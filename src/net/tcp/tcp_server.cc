#include <sys/socket.h>
#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include "net/tcp/tcp_server.h"
#include "net/tcp/tcp_connection.h"
#include "net/tcp/io_thread.h"
#include "net/tcp/tcp_connection_time_wheel.h"
#include "coroutine/coroutine.h"
#include "coroutine/coroutine_hook.h"
#include "coroutine/coroutine_pool.h"
#include "comm/config.h"
#include "rpc/rpc_codec.h"
#include "rpc/rpc_dispatcher.h"

namespace crpc {

TcpAcceptor::TcpAcceptor(NetAddress::ptr net_addr) : m_local_addr(net_addr) {
  m_family = m_local_addr->getFamily();
}

void TcpAcceptor::init() {
  m_fd = socket(m_local_addr->getFamily(), SOCK_STREAM, 0);
  if (m_fd < 0) {
    ErrorLog << "start server error. socket error, sys error=" << strerror(errno);
    abort();
  }
  DebugLog << "create listenfd succ, listenfd=" << m_fd;

  int val = 1;
  if (setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0) {
    ErrorLog << "set REUSEADDR error";
  }

  socklen_t len = m_local_addr->getSockLen();
  int rt = bind(m_fd, m_local_addr->getSockAddr(), len);
  if (rt != 0) {
    ErrorLog << "start server error. bind error, errno=" << errno << ", error=" << strerror(errno);
    abort();
  }

  DebugLog << "set REUSEADDR succ";
  rt = listen(m_fd, 10);
  if (rt != 0) {
    ErrorLog << "start server error. listen error, fd= " << m_fd << ", errno=" << errno << ", error=" << strerror(errno);
    abort();
  }
}

TcpAcceptor::~TcpAcceptor() {
  FdEvent::ptr fd_event = FdEventContainer::GetFdContainer()->getFdEvent(m_fd);
  fd_event->unregisterFromReactor();
  if (m_fd != -1) {
    close(m_fd);
  }
}

int TcpAcceptor::toAccept() {
  socklen_t len = 0;
  int rt = 0;

  if (m_family == AF_INET) {
    sockaddr_in cli_addr;
    memset(&cli_addr, 0, sizeof(cli_addr));
    len = sizeof(cli_addr);
    rt = accept_hook(m_fd, reinterpret_cast<sockaddr*>(&cli_addr), &len);
    if (rt == -1) {
      DebugLog << "error, no new client coming, errno=" << errno << "error=" << strerror(errno);
      return -1;
    }
    InfoLog << "New client accepted succ! port:[" << cli_addr.sin_port;
    m_peer_addr = std::make_shared<IPAddress>(cli_addr);
  } else if (m_family == AF_UNIX) {
    sockaddr_un cli_addr;
    len = sizeof(cli_addr);
    memset(&cli_addr, 0, sizeof(cli_addr));
    rt = accept_hook(m_fd, reinterpret_cast<sockaddr*>(&cli_addr), &len);
    if (rt == -1) {
      DebugLog << "error, no new client coming, errno=" << errno << "error=" << strerror(errno);
      return -1;
    }
    m_peer_addr = std::make_shared<UnixDomainAddress>(cli_addr);
  } else {
    ErrorLog << "unknown type protocol!";
    close(rt);
    return -1;
  }

  InfoLog << "New client accepted succ! fd:[" << rt << ", addr:[" << m_peer_addr->toString() << "]";
  return rt;
}

TcpServer::TcpServer(NetAddress::ptr addr) : m_addr(addr) {
  m_io_pool = std::make_shared<IOThreadPool>(GetConfig()->m_iothread_num);

  m_dispatcher = std::make_shared<RpcDispatcher>();
  m_codec = std::make_shared<RpcCodeC>();

  m_main_reactor = Reactor::GetReactor();
  m_main_reactor->setReactorType(MainReactor);

  m_time_wheel = std::make_shared<TcpTimeWheel>(m_main_reactor, GetConfig()->m_timewheel_bucket_num, GetConfig()->m_timewheel_inteval);

  m_clear_clent_timer_event = std::make_shared<TimerEvent>(10000, true, std::bind(&TcpServer::ClearClientTimerFunc, this));
  m_main_reactor->getTimer()->addTimerEvent(m_clear_clent_timer_event);

  InfoLog << "TcpServer setup on [" << m_addr->toString() << "]";
}

void TcpServer::start() {
  m_acceptor.reset(new TcpAcceptor(m_addr));
  m_acceptor->init();
  m_accept_cor = GetCoroutinePool()->getCoroutineInstanse();
  m_accept_cor->setCallBack(std::bind(&TcpServer::MainAcceptCorFunc, this));

  InfoLog << "resume accept coroutine";
  Coroutine::Resume(m_accept_cor.get());

  m_io_pool->start();
  m_main_reactor->loop();
}

TcpServer::~TcpServer() {
  GetCoroutinePool()->returnCoroutine(m_accept_cor);
  DebugLog << "~TcpServer";
}

void TcpServer::MainAcceptCorFunc() {
  while (!m_is_stop_accept) {
    int fd = m_acceptor->toAccept();
    if (fd == -1) {
      ErrorLog << "accept ret -1 error, return, to yield";
      Coroutine::Yield();
      continue;
    }
    IOThread* io_thread = m_io_pool->getIOThread();
    TcpConnection::ptr conn = addClient(io_thread, fd);
    conn->initServer();
    DebugLog << "tcpconnection address is " << conn.get() << ", and fd is" << fd;

    io_thread->getReactor()->addCoroutine(conn->getCoroutine());
    m_tcp_counts++;
    DebugLog << "current tcp connection count is [" << m_tcp_counts << "]";
  }
}

void TcpServer::addCoroutine(Coroutine::ptr cor) {
  m_main_reactor->addCoroutine(cor);
}

bool TcpServer::registerService(std::shared_ptr<google::protobuf::Service> service) {
  if (service) {
    dynamic_cast<RpcDispatcher*>(m_dispatcher.get())->registerService(service);
  } else {
    ErrorLog << "register service error, service ptr is nullptr";
    return false;
  }
  return true;
}

TcpConnection::ptr TcpServer::addClient(IOThread* io_thread, int fd) {
  auto it = m_clients.find(fd);
  if (it != m_clients.end()) {
    it->second.reset();
    DebugLog << "fd " << fd << "have exist, reset it";
    it->second = std::make_shared<TcpConnection>(this, io_thread, fd, 128, getPeerAddr());
    return it->second;
  } else {
    DebugLog << "fd " << fd << "did't exist, new it";
    TcpConnection::ptr conn = std::make_shared<TcpConnection>(this, io_thread, fd, 128, getPeerAddr());
    m_clients.insert(std::make_pair(fd, conn));
    return conn;
  }
}

void TcpServer::freshTcpConnection(TcpTimeWheel::TcpConnectionSlot::ptr slot) {
  auto cb = [slot, this]() mutable {
    this->getTimeWheel()->fresh(slot);
    slot.reset();
  };
  m_main_reactor->addTask(cb);
}

void TcpServer::ClearClientTimerFunc() {
  for (auto& i : m_clients) {
    if (i.second && i.second.use_count() > 0 && i.second->getState() == Closed) {
      DebugLog << "TcpConection [fd:" << i.first << "] will delete, state=" << i.second->getState();
      (i.second).reset();
    }
  }
}

NetAddress::ptr TcpServer::getPeerAddr() {
  return m_acceptor->getPeerAddr();
}

NetAddress::ptr TcpServer::getLocalAddr() {
  return m_addr;
}

TcpTimeWheel::ptr TcpServer::getTimeWheel() {
  return m_time_wheel;
}

IOThreadPool::ptr TcpServer::getIOThreadPool() {
  return m_io_pool;
}

AbstractDispatcher::ptr TcpServer::getDispatcher() {
  return m_dispatcher;
}

AbstractCodeC::ptr TcpServer::getCodec() {
  return m_codec;
}

}  // namespace crpc
