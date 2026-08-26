#pragma once

#include <google/protobuf/message.h>
#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>
#include <map>
#include <memory>
#include "net/abstract_dispatcher.h"
#include "rpc/rpc_data.h"

namespace crpc {

class RpcDispatcher : public AbstractDispatcher {
 public:
  typedef std::shared_ptr<google::protobuf::Service> service_ptr;

  RpcDispatcher() = default;
  ~RpcDispatcher() = default;

  void dispatch(AbstractData* data, TcpConnection* conn);

  void registerService(service_ptr service);

 public:
  // key: service name
  std::map<std::string, service_ptr> m_service_map;
};

}  // namespace crpc
