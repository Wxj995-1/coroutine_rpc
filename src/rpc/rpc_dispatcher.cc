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
    ErrorLog << "dynamic_cast error";
    return;
  }
  Coroutine::GetCurrentCoroutine()->getRunTime()->m_msg_no = tmp->msg_no;
  setCurrentRunTime(Coroutine::GetCurrentCoroutine()->getRunTime());

  InfoLog << "begin to dispatch client request, msgno=" << tmp->msg_no;

  std::string service_name = tmp->service_name;
  std::string method_name = tmp->method_name;

  RpcStruct reply_pk;
  reply_pk.msg_no = tmp->msg_no;
  reply_pk.service_name = service_name;
  reply_pk.method_name = method_name;

  auto it = m_service_map.find(service_name);
  if (it == m_service_map.end() || !((*it).second)) {
    reply_pk.err_code = ERROR_SERVICE_NOT_FOUND;
    std::stringstream ss;
    ss << "not found service_name:[" << service_name << "]";
    ErrorLog << reply_pk.msg_no << "|" << ss.str();
    reply_pk.err_info = ss.str();

    conn->getCodec()->encode(conn->getOutBuffer(), dynamic_cast<AbstractData*>(&reply_pk));
    InfoLog << "end dispatch client request, msgno=" << tmp->msg_no;
    return;
  }

  service_ptr service = (*it).second;

  const google::protobuf::MethodDescriptor* method = service->GetDescriptor()->FindMethodByName(method_name);
  if (!method) {
    reply_pk.err_code = ERROR_METHOD_NOT_FOUND;
    std::stringstream ss;
    ss << "not found method_name:[" << method_name << "]";
    ErrorLog << reply_pk.msg_no << "|" << ss.str();
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
    ErrorLog << reply_pk.msg_no << "|" << ss.str();
    delete request;
    conn->getCodec()->encode(conn->getOutBuffer(), dynamic_cast<AbstractData*>(&reply_pk));
    return;
  }

  InfoLog << "============================================================";
  InfoLog << reply_pk.msg_no << "|Get client request data:" << request->ShortDebugString();
  InfoLog << "============================================================";

  google::protobuf::Message* response = service->GetResponsePrototype(method).New();

  // note: service->CallMethod is expected to run synchronously and fill response.
  // the closure is a no-op; the framework serializes the response right after CallMethod returns.
  RpcClosure closure([]() {});
  service->CallMethod(method, nullptr, request, response, &closure);

  InfoLog << "Call [" << service_name << "." << method_name << "] succ, now send reply package";

  if (!(response->SerializeToString(&(reply_pk.pb_data)))) {
    reply_pk.pb_data = "";
    ErrorLog << reply_pk.msg_no << "|reply error! encode reply package error";
    reply_pk.err_code = ERROR_FAILED_SERIALIZE;
    reply_pk.err_info = "failed to serilize relpy data";
  } else {
    InfoLog << "============================================================";
    InfoLog << reply_pk.msg_no << "|Set server response data:" << response->ShortDebugString();
    InfoLog << "============================================================";
  }

  delete request;
  delete response;

  conn->getCodec()->encode(conn->getOutBuffer(), dynamic_cast<AbstractData*>(&reply_pk));
}

void RpcDispatcher::registerService(service_ptr service) {
  std::string service_name = service->GetDescriptor()->name();
  m_service_map[service_name] = service;
  InfoLog << "succ register service[" << service_name << "]!";
}

}  // namespace crpc
