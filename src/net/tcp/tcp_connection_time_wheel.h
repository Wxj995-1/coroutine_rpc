#pragma once

#include <queue>
#include <vector>
#include "net/tcp/abstract_slot.h"
#include "net/reactor.h"
#include "net/timer.h"

namespace crpc {

class TcpConnection;

class TcpTimeWheel {
 public:
  typedef std::shared_ptr<TcpTimeWheel> ptr;

  typedef AbstractSlot<TcpConnection> TcpConnectionSlot;

  TcpTimeWheel(Reactor* reactor, int bucket_count, int invetal = 10);

  ~TcpTimeWheel();

  void fresh(TcpConnectionSlot::ptr slot);

  void loopFunc();

 private:
  Reactor* m_reactor {nullptr};
  int m_bucket_count {0};
  int m_inteval {0};  // second

  TimerEvent::ptr m_event;
  std::queue<std::vector<TcpConnectionSlot::ptr>> m_wheel;
};

}  // namespace crpc
