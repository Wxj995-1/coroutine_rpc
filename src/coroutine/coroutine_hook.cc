#include <assert.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "coroutine/coroutine_hook.h"
#include "coroutine/coroutine.h"
#include "net/fd_event.h"
#include "net/reactor.h"
#include "net/timer.h"
#include "comm/log.h"
#include "comm/config.h"

#define HOOK_SYS_FUNC(name) name##_fun_ptr_t g_sys_##name##_fun = (name##_fun_ptr_t)dlsym(RTLD_NEXT, #name);

HOOK_SYS_FUNC(accept);
HOOK_SYS_FUNC(read);
HOOK_SYS_FUNC(write);
HOOK_SYS_FUNC(connect);
HOOK_SYS_FUNC(sleep);

namespace crpc {

static bool g_hook = true;

void SetHook(bool value) {
  g_hook = value;
}

void toEpoll(FdEvent::ptr fd_event, int events) {
  Coroutine* cur_cor = Coroutine::GetCurrentCoroutine();
  if (events & IOEvent::READ) {
    DebugLog << "fd:[" << fd_event->getFd() << "], register read event to epoll";
    fd_event->setCoroutine(cur_cor);
    fd_event->addListenEvents(IOEvent::READ);
  }
  if (events & IOEvent::WRITE) {
    DebugLog << "fd:[" << fd_event->getFd() << "], register write event to epoll";
    fd_event->setCoroutine(cur_cor);
    fd_event->addListenEvents(IOEvent::WRITE);
  }
}

ssize_t read_hook(int fd, void* buf, size_t count) {
  DebugLog << "this is hook read";
  if (Coroutine::IsMainCoroutine()) {
    DebugLog << "hook disable, call sys read func";
    return g_sys_read_fun(fd, buf, count);
  }

  Reactor::GetReactor();

  FdEvent::ptr fd_event = FdEventContainer::GetFdContainer()->getFdEvent(fd);
  if (fd_event->getReactor() == nullptr) {
    fd_event->setReactor(Reactor::GetReactor());
  }

  fd_event->setNonBlock();

  ssize_t n = g_sys_read_fun(fd, buf, count);
  if (n > 0) {
    return n;
  }

  toEpoll(fd_event, IOEvent::READ);

  DebugLog << "read func to yield";
  Coroutine::Yield();

  fd_event->delListenEvents(IOEvent::READ);
  fd_event->clearCoroutine();

  DebugLog << "read func yield back, now to call sys read";
  return g_sys_read_fun(fd, buf, count);
}

int accept_hook(int sockfd, struct sockaddr* addr, socklen_t* addrlen) {
  DebugLog << "this is hook accept";
  if (Coroutine::IsMainCoroutine()) {
    DebugLog << "hook disable, call sys accept func";
    return g_sys_accept_fun(sockfd, addr, addrlen);
  }
  Reactor::GetReactor();

  FdEvent::ptr fd_event = FdEventContainer::GetFdContainer()->getFdEvent(sockfd);
  if (fd_event->getReactor() == nullptr) {
    fd_event->setReactor(Reactor::GetReactor());
  }

  fd_event->setNonBlock();

  int n = g_sys_accept_fun(sockfd, addr, addrlen);
  if (n > 0) {
    return n;
  }

  toEpoll(fd_event, IOEvent::READ);

  DebugLog << "accept func to yield";
  Coroutine::Yield();

  fd_event->delListenEvents(IOEvent::READ);
  fd_event->clearCoroutine();

  DebugLog << "accept func yield back, now to call sys accept";
  return g_sys_accept_fun(sockfd, addr, addrlen);
}

ssize_t write_hook(int fd, const void* buf, size_t count) {
  DebugLog << "this is hook write";
  if (Coroutine::IsMainCoroutine()) {
    DebugLog << "hook disable, call sys write func";
    return g_sys_write_fun(fd, buf, count);
  }
  Reactor::GetReactor();

  FdEvent::ptr fd_event = FdEventContainer::GetFdContainer()->getFdEvent(fd);
  if (fd_event->getReactor() == nullptr) {
    fd_event->setReactor(Reactor::GetReactor());
  }

  fd_event->setNonBlock();

  ssize_t n = g_sys_write_fun(fd, buf, count);
  if (n > 0) {
    return n;
  }

  toEpoll(fd_event, IOEvent::WRITE);

  DebugLog << "write func to yield";
  Coroutine::Yield();

  fd_event->delListenEvents(IOEvent::WRITE);
  fd_event->clearCoroutine();

  DebugLog << "write func yield back, now to call sys write";
  return g_sys_write_fun(fd, buf, count);
}

int connect_hook(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
  DebugLog << "this is hook connect";
  if (Coroutine::IsMainCoroutine()) {
    DebugLog << "hook disable, call sys connect func";
    return g_sys_connect_fun(sockfd, addr, addrlen);
  }
  Reactor* reactor = Reactor::GetReactor();

  FdEvent::ptr fd_event = FdEventContainer::GetFdContainer()->getFdEvent(sockfd);
  if (fd_event->getReactor() == nullptr) {
    fd_event->setReactor(reactor);
  }
  Coroutine* cur_cor = Coroutine::GetCurrentCoroutine();

  fd_event->setNonBlock();
  int n = g_sys_connect_fun(sockfd, addr, addrlen);
  if (n == 0) {
    DebugLog << "direct connect succ, return";
    return n;
  } else if (errno != EINPROGRESS) {
    DebugLog << "connect error and errno is't EINPROGRESS, errno=" << errno << ",error=" << strerror(errno);
    return n;
  }

  DebugLog << "errno == EINPROGRESS";

  toEpoll(fd_event, IOEvent::WRITE);

  bool is_timeout = false;

  auto timeout_cb = [&is_timeout, cur_cor]() {
    is_timeout = true;
    Coroutine::Resume(cur_cor);
  };

  TimerEvent::ptr event = std::make_shared<TimerEvent>(GetConfig()->m_max_connect_timeout, false, timeout_cb);

  Timer* timer = reactor->getTimer();
  timer->addTimerEvent(event);

  Coroutine::Yield();

  fd_event->delListenEvents(IOEvent::WRITE);
  fd_event->clearCoroutine();

  timer->delTimerEvent(event);

  n = g_sys_connect_fun(sockfd, addr, addrlen);
  if ((n < 0 && errno == EISCONN) || n == 0) {
    DebugLog << "connect succ";
    return 0;
  }

  if (is_timeout) {
    ErrorLog << "connect error,  timeout[ " << GetConfig()->m_max_connect_timeout << "ms]";
    errno = ETIMEDOUT;
  }

  DebugLog << "connect error and errno=" << errno << ", error=" << strerror(errno);
  return -1;
}

unsigned int sleep_hook(unsigned int seconds) {
  DebugLog << "this is hook sleep";
  if (Coroutine::IsMainCoroutine()) {
    DebugLog << "hook disable, call sys sleep func";
    return g_sys_sleep_fun(seconds);
  }

  Coroutine* cur_cor = Coroutine::GetCurrentCoroutine();

  bool is_timeout = false;
  auto timeout_cb = [cur_cor, &is_timeout]() {
    DebugLog << "onTime, now resume sleep cor";
    is_timeout = true;
    Coroutine::Resume(cur_cor);
  };

  TimerEvent::ptr event = std::make_shared<TimerEvent>(1000 * seconds, false, timeout_cb);

  Reactor::GetReactor()->getTimer()->addTimerEvent(event);

  DebugLog << "now to yield sleep";
  while (!is_timeout) {
    Coroutine::Yield();
  }

  return 0;
}

}  // namespace crpc

extern "C" {

int accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen) {
  if (!crpc::g_hook) {
    return g_sys_accept_fun(sockfd, addr, addrlen);
  } else {
    return crpc::accept_hook(sockfd, addr, addrlen);
  }
}

ssize_t read(int fd, void* buf, size_t count) {
  if (!crpc::g_hook) {
    return g_sys_read_fun(fd, buf, count);
  } else {
    return crpc::read_hook(fd, buf, count);
  }
}

ssize_t write(int fd, const void* buf, size_t count) {
  if (!crpc::g_hook) {
    return g_sys_write_fun(fd, buf, count);
  } else {
    return crpc::write_hook(fd, buf, count);
  }
}

int connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
  if (!crpc::g_hook) {
    return g_sys_connect_fun(sockfd, addr, addrlen);
  } else {
    return crpc::connect_hook(sockfd, addr, addrlen);
  }
}

unsigned int sleep(unsigned int seconds) {
  if (!crpc::g_hook) {
    return g_sys_sleep_fun(seconds);
  } else {
    return crpc::sleep_hook(seconds);
  }
}

}
