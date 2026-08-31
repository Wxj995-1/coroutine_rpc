#include <google/protobuf/message.h>
#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>
#include <sstream>
#include "net/abstract_dispatcher.h"
#include "net/error_code.h"
#include "rpc/rpc_data.h"
#include "rpc/rpc_dispatcher.h"
#include "rpc/rpc_closure.h"
#include "rpc/rpc_codec.h"
#include "comm/msg_req.h"
#include "coroutine/coroutine.h"
#include "net/tcp/tcp_connection.h"

namespace crpc {

void RpcDispatcher::dispatch(AbstractData* data, TcpConnection* conn) {
  RpcStruct* tmp = dynamic_cast<RpcStruct*>(data);

  if (tmp == nullptr) {
    ErrorLog << "event=rpc_dispatch_failed stage=request_cast"
             << " reason=invalid_rpc_data";
    return;
  }
  Coroutine::GetCurrentCoroutine()->getRunTime()->m_msg_no = tmp->msg_no;
  setCurrentRunTime(Coroutine::GetCurrentCoroutine()->getRunTime());

  InfoLog << "event=rpc_dispatch_started request_id=" << tmp->msg_no
          << " service=" << tmp->service_name
          << " method=" << tmp->method_name
          << " request_payload_bytes=" << tmp->pb_data.size();

  std::string service_name = tmp->service_name;
  std::string method_name = tmp->method_name;
  Coroutine::GetCurrentCoroutine()->getRunTime()->m_interface_name =
      service_name + "." + method_name;

  RpcStruct reply_pk;
  reply_pk.msg_no = tmp->msg_no;
  reply_pk.service_name = service_name;
  reply_pk.method_name = method_name;

  auto it = m_service_map.find(service_name);
  if (it == m_service_map.end() || !((*it).second)) {
    reply_pk.err_code = ERROR_SERVICE_NOT_FOUND;
    std::stringstream ss;
    ss << "not found service_name:[" << service_name << "]";
    ErrorLog << "event=rpc_dispatch_failed stage=service_lookup"
             << " err_code=" << reply_pk.err_code
             << " service=" << service_name
             << " action=send_error_response";
    reply_pk.err_info = ss.str();

    conn->getCodec()->encode(conn->getOutBuffer(), dynamic_cast<AbstractData*>(&reply_pk));
    return;
  }

  service_ptr service = (*it).second;

  const google::protobuf::MethodDescriptor* method = service->GetDescriptor()->FindMethodByName(method_name);
  if (!method) {
    reply_pk.err_code = ERROR_METHOD_NOT_FOUND;
    std::stringstream ss;
    ss << "not found method_name:[" << method_name << "]";
    ErrorLog << "event=rpc_dispatch_failed stage=method_lookup"
             << " err_code=" << reply_pk.err_code
             << " service=" << service_name
             << " method=" << method_name
             << " action=send_error_response";
    reply_pk.err_info = ss.str();
    conn->getCodec()->encode(conn->getOutBuffer(), dynamic_cast<AbstractData*>(&reply_pk));
    return;
  }

  google::protobuf::Message* request = service->GetRequestPrototype(method).New();

  if (!request->ParseFromString(tmp->pb_data)) {
    reply_pk.err_code = ERROR_FAILED_SERIALIZE;
    std::stringstream ss;
    ss << "faild to parse request data, request.name:[" << request->GetDescriptor()->full_name() << "]";
    reply_pk.err_info = ss.str();
    ErrorLog << "event=rpc_dispatch_failed stage=request_deserialize"
             << " err_code=" << reply_pk.err_code
             << " message_type=" << request->GetDescriptor()->full_name()
             << " request_payload_bytes=" << tmp->pb_data.size()
             << " action=send_error_response";
    delete request;
    conn->getCodec()->encode(conn->getOutBuffer(), dynamic_cast<AbstractData*>(&reply_pk));
    return;
  }

  DebugLog << "event=rpc_request payload=" << request->ShortDebugString();

  google::protobuf::Message* response = service->GetResponsePrototype(method).New();

  // note: service->CallMethod is expected to run synchronously and fill response.
  // the closure is a no-op; the framework serializes the response right after CallMethod returns.
  RpcClosure closure([]() {});
  service->CallMethod(method, nullptr, request, response, &closure);

  if (!(response->SerializeToString(&(reply_pk.pb_data)))) {
    reply_pk.pb_data = "";
    ErrorLog << "event=rpc_dispatch_failed stage=response_serialize"
             << " err_code=" << ERROR_FAILED_SERIALIZE
             << " message_type=" << response->GetDescriptor()->full_name();
    reply_pk.err_code = ERROR_FAILED_SERIALIZE;
    reply_pk.err_info = "failed to serilize relpy data";
  } else {
    DebugLog << "event=rpc_response payload=" << response->ShortDebugString();
  }

  delete request;
  delete response;

  conn->getCodec()->encode(conn->getOutBuffer(), dynamic_cast<AbstractData*>(&reply_pk));
  InfoLog << "event=rpc_dispatch_completed status="
          << (reply_pk.err_code == 0 ? "success" : "failure")
          << " err_code=" << reply_pk.err_code
          << " response_payload_bytes=" << reply_pk.pb_data.size();
}

void RpcDispatcher::registerService(service_ptr service) {
  std::string service_name = service->GetDescriptor()->name();
  m_service_map[service_name] = service;
  InfoLog << "event=service_registered service=" << service_name;
}

}  // namespace crpc
