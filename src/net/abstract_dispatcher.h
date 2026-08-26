#pragma once

#include <memory>
#include <google/protobuf/service.h>
#include "net/abstract_data.h"
#include "net/tcp/tcp_connection.h"

namespace crpc {

class TcpConnection;

class AbstractDispatcher {
 public:
  typedef std::shared_ptr<AbstractDispatcher> ptr;

  AbstractDispatcher() {}

  virtual ~AbstractDispatcher() {}

  virtual void dispatch(AbstractData* data, TcpConnection* conn) = 0;
};

}  // namespace crpc
