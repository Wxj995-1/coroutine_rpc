#pragma once

#include <sys/socket.h>
#include <sys/types.h>
#include <vector>
#include <atomic>
#include <map>
#include <functional>
#include <queue>
#include "coroutine/coroutine.h"
#include "net/fd_event.h"
#include "net/mutex.h"

namespace crpc {

enum ReactorType {
  MainReactor = 1,  // main reactor, only set this by main thread.
  SubReactor = 2    // child reactor, every io thread is this type
};

class FdEvent;
class Timer;

class Reactor {
 public:
  typedef std::shared_ptr<Reactor> ptr;

  explicit Reactor();

  ~Reactor();

  void addEvent(int fd, epoll_event event, bool is_wakeup = true);

  void delEvent(int fd, bool is_wakeup = true);

  void addTask(std::function<void()> task, bool is_wakeup = true);

  void addTask(std::vector<std::function<void()>> task, bool is_wakeup = true);

  void addCoroutine(Coroutine::ptr cor, bool is_wakeup = true);

  void wakeup();

  void loop();

  void stop();

  Timer* getTimer();

  pid_t getTid();

  void setReactorType(ReactorType type);

 public:
  static Reactor* GetReactor();

 private:
  void addWakeupFd();

  bool isLoopThread() const;

  void addEventInLoopThread(int fd, epoll_event event);

  void delEventInLoopThread(int fd);

 private:
  int m_epfd {-1};
  int m_wake_fd {-1};    // wakeup fd
  int m_timer_fd {-1};   // timer fd
  bool m_stop_flag {false};
  bool m_is_looping {false};
  bool m_is_init_timer {false};
  pid_t m_tid {0};       // thread id

  Mutex m_mutex;

  std::vector<int> m_fds;             // already care events
  std::atomic<int> m_fd_size;

  std::map<int, epoll_event> m_pending_add_fds;
  std::vector<int> m_pending_del_fds;
  std::vector<std::function<void()>> m_pending_tasks;

  Timer* m_timer {nullptr};

  ReactorType m_reactor_type {SubReactor};
};

class CoroutineTaskQueue {
 public:
  static CoroutineTaskQueue* GetCoroutineTaskQueue();

  void push(FdEvent* fd);

  FdEvent* pop();

 private:
  std::queue<FdEvent*> m_task;
  Mutex m_mutex;
};

}  // namespace crpc
