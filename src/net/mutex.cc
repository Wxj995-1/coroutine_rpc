#pragma once

#include <pthread.h>
#include <memory>
#include "net/mutex.h"
#include "net/reactor.h"
#include "comm/log.h"
#include "coroutine/coroutine.h"
#include "coroutine/coroutine_hook.h"

namespace crpc {

CoroutineMutex::CoroutineMutex() {}

CoroutineMutex::~CoroutineMutex() {
  if (m_lock) {
    unlock();
  }
}

void CoroutineMutex::lock() {
  if (Coroutine::IsMainCoroutine()) {
    ErrorLog << "main coroutine can't use coroutine mutex";
    return;
  }

  Coroutine* cor = Coroutine::GetCurrentCoroutine();

  Mutex::Lock lock(m_mutex);
  if (!m_lock) {
    m_lock = true;
    DebugLog << "coroutine succ get coroutine mutex";
    lock.unlock();
  } else {
    m_sleep_cors.push(cor);
    auto tmp = m_sleep_cors;
    lock.unlock();

    DebugLog << "coroutine yield, pending coroutine mutex, current sleep queue exist ["
             << tmp.size() << "] coroutines";

    Coroutine::Yield();
  }
}

void CoroutineMutex::unlock() {
  if (Coroutine::IsMainCoroutine()) {
    ErrorLog << "main coroutine can't use coroutine mutex";
    return;
  }

  Mutex::Lock lock(m_mutex);
  if (m_lock) {
    m_lock = false;
    if (m_sleep_cors.empty()) {
      return;
    }

    Coroutine* cor = m_sleep_cors.front();
    m_sleep_cors.pop();
    lock.unlock();

    if (cor) {
      DebugLog << "coroutine unlock, now to resume coroutine[" << cor->getCorId() << "]";

      Reactor::GetReactor()->addTask([cor]() {
        Coroutine::Resume(cor);
      }, true);
    }
  }
}

}  // namespace crpc
