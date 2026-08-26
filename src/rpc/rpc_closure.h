#pragma once

#include <google/protobuf/stubs/callback.h>
#include <functional>
#include <memory>

namespace crpc {

class RpcClosure : public google::protobuf::Closure {
 public:
  typedef std::shared_ptr<RpcClosure> ptr;

  explicit RpcClosure(std::function<void()> cb) : m_cb(cb) {}

  ~RpcClosure() = default;

  void Run() {
    if (m_cb) {
      m_cb();
    }
  }

 private:
  std::function<void()> m_cb {nullptr};
};

}  // namespace crpc
