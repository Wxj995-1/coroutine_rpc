#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include "net/tcp/tcp_connection.h"
#include "net/tcp/tcp_server.h"
#include "net/tcp/tcp_client.h"
#include "rpc/rpc_data.h"
#include "coroutine/coroutine_hook.h"
#include "coroutine/coroutine_pool.h"
#include "net/tcp/tcp_connection_time_wheel.h"
#include "net/tcp/abstract_slot.h"
#include "net/timer.h"

namespace crpc {

TcpConnection::TcpConnection(TcpServer* tcp_svr, IOThread* io_thread, int fd, int buff_size, NetAddress::ptr peer_addr)
    : m_io_thread(io_thread), m_fd(fd), m_state(Connected), m_connection_type(ServerConnection), m_peer_addr(peer_addr) {
  m_reactor = m_io_thread->getReactor();

  m_tcp_svr = tcp_svr;

  m_codec = m_tcp_svr->getCodec();
  m_fd_event = FdEventContainer::GetFdContainer()->getFdEvent(fd);
  m_fd_event->setReactor(m_reactor);
  initBuffer(buff_size);
  m_loop_cor = GetCoroutinePool()->getCoroutineInstanse();
  m_state = Connected;
  DebugLog << "event=connection_created type=server fd=" << fd
           << " peer=" << m_peer_addr->toString() << " state=" << m_state;
}

TcpConnection::TcpConnection(TcpClient* tcp_cli, Reactor* reactor, int fd, int buff_size, NetAddress::ptr peer_addr)
    : m_fd(fd), m_state(NotConnected), m_connection_type(ClientConnection), m_peer_addr(peer_addr) {
  m_reactor = reactor;

  m_tcp_cli = tcp_cli;

  m_codec = m_tcp_cli->getCodeC();

  m_fd_event = FdEventContainer::GetFdContainer()->getFdEvent(fd);
  m_fd_event->setReactor(m_reactor);
  initBuffer(buff_size);

  DebugLog << "event=connection_created type=client fd=" << fd
           << " peer=" << m_peer_addr->toString() << " state=" << m_state;
}

void TcpConnection::initServer() {
  registerToTimeWheel();
  m_loop_cor->setCallBack(std::bind(&TcpConnection::MainServerLoopCorFunc, this));
}

void TcpConnection::setUpServer() {
  m_reactor->addCoroutine(m_loop_cor);
}

void TcpConnection::registerToTimeWheel() {
  auto cb = [](TcpConnection::ptr conn) {
    conn->shutdownConnection();
  };
  TcpTimeWheel::TcpConnectionSlot::ptr tmp = std::make_shared<AbstractSlot<TcpConnection>>(shared_from_this(), cb);
  m_weak_slot = tmp;
  m_tcp_svr->freshTcpConnection(tmp);
}

void TcpConnection::setUpClient() {
  setState(Connected);
}

TcpConnection::~TcpConnection() {
  if (m_connection_type == ServerConnection) {
    GetCoroutinePool()->returnCoroutine(m_loop_cor);
  }

  DebugLog << "event=connection_destroyed fd=" << m_fd
           << " peer=" << m_peer_addr->toString();
}

void TcpConnection::initBuffer(int size) {
  m_write_buffer = std::make_shared<TcpBuffer>(size);
  m_read_buffer = std::make_shared<TcpBuffer>(size);
}

void TcpConnection::MainServerLoopCorFunc() {
  while (!m_stop) {
    input();

    execute();

    output();
  }
  DebugLog << "event=connection_loop_stopped fd=" << m_fd
           << " peer=" << m_peer_addr->toString();
}

void TcpConnection::input() {
  if (m_is_over_time) {
    WarnLog << "event=read_skipped fd=" << m_fd
            << " peer=" << m_peer_addr->toString() << " reason=timeout";
    return;
  }
  TcpConnectionState state = getState();
  if (state == Closed || state == NotConnected) {
    return;
  }
  bool read_all = false;
  bool close_flag = false;
  int count = 0;
  while (!read_all) {
    if (m_read_buffer->writeAble() == 0) {
      m_read_buffer->resizeBuffer(2 * m_read_buffer->getSize());
    }

    int read_count = m_read_buffer->writeAble();
    int write_index = m_read_buffer->writeIndex();

    int rt = read_hook(m_fd, &(m_read_buffer->m_buffer[write_index]), read_count);
    if (rt > 0) {
      m_read_buffer->recycleWrite(rt);
    }

    count += rt;
    if (m_is_over_time) {
      WarnLog << "event=read_stopped fd=" << m_fd
              << " peer=" << m_peer_addr->toString() << " reason=timeout";
      break;
    }
    if (rt <= 0) {
      if (rt == 0) {
        InfoLog << "event=connection_closed fd=" << m_fd
                << " peer=" << m_peer_addr->toString()
                << " reason=peer_eof action=close_connection";
      } else {
        ErrorLog << "event=read_failed fd=" << m_fd
                 << " peer=" << m_peer_addr->toString()
                 << " result=" << rt << " errno=" << errno
                 << " error=\"" << strerror(errno)
                 << "\" action=close_connection";
      }
      close_flag = true;
      break;
    } else {
      if (rt == read_count) {
        DebugLog << "event=read_buffer_filled fd=" << m_fd
                 << " requested_bytes=" << read_count << " actual_bytes=" << rt;
        continue;
      } else if (rt < read_count) {
        read_all = true;
        break;
      }
    }
  }
  if (close_flag) {
    clearClient();
    DebugLog << "event=connection_cleanup_wait fd=" << m_fd
             << " peer=" << m_peer_addr->toString();
    Coroutine::GetCurrentCoroutine()->setCanResume(false);
    Coroutine::Yield();
  }

  if (m_is_over_time) {
    return;
  }

  InfoLog << "event=data_received fd=" << m_fd
          << " peer=" << m_peer_addr->toString() << " bytes=" << count;
  if (m_connection_type == ServerConnection) {
    TcpTimeWheel::TcpConnectionSlot::ptr tmp = m_weak_slot.lock();
    if (tmp) {
      m_tcp_svr->freshTcpConnection(tmp);
    }
  }
}

void TcpConnection::execute() {
  while (m_read_buffer->readAble() > 0) {
    std::shared_ptr<AbstractData> data = std::make_shared<RpcStruct>();

    m_codec->decode(m_read_buffer.get(), data.get());
    if (!data->decode_succ) {
      ErrorLog << "event=request_decode_failed fd=" << m_fd
               << " peer=" << m_peer_addr->toString()
               << " readable_bytes=" << m_read_buffer->readAble();
      break;
    }
    if (m_connection_type == ServerConnection) {
      m_tcp_svr->getDispatcher()->dispatch(data.get(), this);
    } else if (m_connection_type == ClientConnection) {
      std::shared_ptr<RpcStruct> tmp = std::dynamic_pointer_cast<RpcStruct>(data);
      if (tmp) {
        m_reply_datas.insert(std::make_pair(tmp->msg_no, tmp));
      }
    }
  }
}

void TcpConnection::output() {
  if (m_is_over_time) {
    WarnLog << "event=write_skipped fd=" << m_fd
            << " peer=" << m_peer_addr->toString() << " reason=timeout";
    return;
  }
  while (true) {
    TcpConnectionState state = getState();
    if (state != Connected) {
      break;
    }

    if (m_write_buffer->readAble() == 0) {
      DebugLog << "event=write_buffer_empty fd=" << m_fd;
      break;
    }

    int total_size = m_write_buffer->readAble();
    int read_index = m_write_buffer->readIndex();
    int rt = write_hook(m_fd, &(m_write_buffer->m_buffer[read_index]), total_size);
    if (rt <= 0) {
      if (rt == 0) {
        WarnLog << "event=write_zero_progress fd=" << m_fd
                << " peer=" << m_peer_addr->toString()
                << " requested_bytes=" << total_size;
      } else {
        ErrorLog << "event=write_failed fd=" << m_fd
                 << " peer=" << m_peer_addr->toString()
                 << " result=" << rt << " errno=" << errno
                 << " error=\"" << strerror(errno) << "\"";
      }
    }

    m_write_buffer->recycleRead(rt);
    if (rt > 0) {
      InfoLog << "event=data_sent fd=" << m_fd
              << " peer=" << m_peer_addr->toString()
              << " bytes=" << rt
              << " remaining_bytes=" << m_write_buffer->readAble();
    }
    if (m_write_buffer->readAble() <= 0) {
      break;
    }

    if (m_is_over_time) {
      WarnLog << "event=write_stopped fd=" << m_fd
              << " peer=" << m_peer_addr->toString() << " reason=timeout";
      break;
    }
  }
}

void TcpConnection::clearClient() {
  if (getState() == Closed) {
    DebugLog << "event=connection_close_skipped fd=" << m_fd
             << " reason=already_closed";
    return;
  }
  m_fd_event->unregisterFromReactor();

  m_stop = true;

  close(m_fd_event->getFd());
  setState(Closed);
}

void TcpConnection::shutdownConnection() {
  TcpConnectionState state = getState();
  if (state == Closed || state == NotConnected) {
    DebugLog << "event=connection_shutdown_skipped fd=" << m_fd
             << " state=" << state;
    return;
  }
  setState(HalfClosing);
  InfoLog << "event=connection_shutdown fd=" << m_fd
          << " peer=" << m_peer_addr->toString();
  shutdown(m_fd_event->getFd(), SHUT_RDWR);
}

TcpBuffer* TcpConnection::getInBuffer() {
  return m_read_buffer.get();
}

TcpBuffer* TcpConnection::getOutBuffer() {
  return m_write_buffer.get();
}

bool TcpConnection::getResPackageData(const std::string& msg_no, RpcStruct::ptr& pb_struct) {
  auto it = m_reply_datas.find(msg_no);
  if (it != m_reply_datas.end()) {
    DebugLog << "event=response_found request_id=" << msg_no;
    pb_struct = it->second;
    m_reply_datas.erase(it);
    return true;
  }
  DebugLog << "event=response_pending request_id=" << msg_no;
  return false;
}

AbstractCodeC::ptr TcpConnection::getCodec() const {
  return m_codec;
}

TcpConnectionState TcpConnection::getState() {
  TcpConnectionState state;
  RWMutex::ReadLock lock(m_mutex);
  state = m_state;
  lock.unlock();

  return state;
}

void TcpConnection::setState(const TcpConnectionState& state) {
  RWMutex::WriteLock lock(m_mutex);
  m_state = state;
  lock.unlock();
}

void TcpConnection::setOverTimeFlag(bool value) {
  m_is_over_time = value;
}

bool TcpConnection::getOverTimerFlag() {
  return m_is_over_time;
}

Coroutine::ptr TcpConnection::getCoroutine() {
  return m_loop_cor;
}

}  // namespace crpc
