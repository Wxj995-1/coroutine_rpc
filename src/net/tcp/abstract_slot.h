#pragma once

#include <memory>
#include <functional>

namespace crpc {

template <class T>
class AbstractSlot {
 public:
  typedef std::shared_ptr<AbstractSlot> ptr;
  typedef std::weak_ptr<T> weakPtr;
  typedef std::shared_ptr<T> sharedPtr;

  AbstractSlot(weakPtr ptr, std::function<void(sharedPtr)> cb) : m_weak_ptr(ptr), m_cb(cb) {}

  ~AbstractSlot() {
    sharedPtr ptr = m_weak_ptr.lock();
    if (ptr) {
      m_cb(ptr);
    }
  }

 private:
  weakPtr m_weak_ptr;
  std::function<void(sharedPtr)> m_cb;
};

}  // namespace crpc
