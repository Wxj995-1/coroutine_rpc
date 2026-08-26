#pragma once

#include <memory>
#include <map>
#include <atomic>
#include <functional>
#include <semaphore.h>
#include "net/reactor.h"
#include "net/tcp/tcp_connection_time_wheel.h"
#include "coroutine/coroutine.h"

namespace crpc {

class TcpServer;

class IOThread {
 public:
  typedef std::shared_ptr<IOThread> ptr;

  IOThread();

  ~IOThread();

  Reactor* getReactor();

  void addClient(TcpConnection* tcp_conn);

  pthread_t getPthreadId();

  void setThreadIndex(const int index);

  int getThreadIndex();

  sem_t* getStartSemaphore();

 public:
  static IOThread* GetCurrentIOThread();

 private:
  static void* main(void* arg);

 private:
  Reactor* m_reactor {nullptr};
  pthread_t m_thread {0};
  pid_t m_tid {-1};
  TimerEvent::ptr m_timer_event {nullptr};
  int m_index {-1};

  sem_t m_init_semaphore;

  sem_t m_start_semaphore;
};

class IOThreadPool {
 public:
  typedef std::shared_ptr<IOThreadPool> ptr;

  IOThreadPool(int size);

  void start();

  IOThread* getIOThread();

  int getIOThreadPoolSize();

  void broadcastTask(std::function<void()> cb);

  void addTaskByIndex(int index, std::function<void()> cb);

  void addCoroutineToRandomThread(Coroutine::ptr cor, bool self = false);

  Coroutine::ptr addCoroutineToRandomThread(std::function<void()> cb, bool self = false);

  Coroutine::ptr addCoroutineToThreadByIndex(int index, std::function<void()> cb, bool self = false);

  void addCoroutineToEachThread(std::function<void()> cb);

 private:
  int m_size {0};

  std::atomic<int> m_index {-1};

  std::vector<IOThread::ptr> m_io_threads;
};

}  // namespace crpc
