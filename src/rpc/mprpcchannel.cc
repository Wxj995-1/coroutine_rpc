#include "rpc/mprpcchannel.h"
#include <string>
#include <stdlib.h>
#include "rpc/mprpccontroller.h"
#include "registry/zookeeperutil.h"
#include "rpc/rpc_data.h"
#include "net/net_address.h"
#include "net/tcp/tcp_client.h"
#include "net/error_code.h"
#include "comm/msg_req.h"

// 所有通过stub代理对象调用的rpc方法，都走到这里了，统一做rpc方法调用的数据序列化和网络发送
void MprpcChannel::CallMethod(const google::protobuf::MethodDescriptor *method,
                              google::protobuf::RpcController *controller,
                              const google::protobuf::Message *request,
                              google::protobuf::Message *response,
                              google::protobuf::Closure *done)
{
  const google::protobuf::ServiceDescriptor *sd = method->service();
  std::string service_name = sd->name();
  std::string method_name = method->name();

  std::string args_str;
  if (!request->SerializeToString(&args_str))
  {
    controller->SetFailed("serialize request error");
    return;
  }

  // 从zk查询目标服务地址（阻塞，在进入协程IO之前完成）
  ZkClient zkCli;
  zkCli.Start();
  std::string method_path = "/" + service_name + "/" + method_name;
  std::string host_data = zkCli.GetData(method_path.c_str());
  if (host_data == "")
  {
    controller->SetFailed(method_path + " is not exist!");
    return;
  }
  int idx = host_data.find(":");
  if (idx == -1)
  {
    controller->SetFailed(method_path + " address is invalid!");
    return;
  }
  std::string ip = host_data.substr(0, idx);
  uint16_t port = atoi(host_data.substr(idx + 1, host_data.size() - idx).c_str());

  MprpcController *rpc_controller = dynamic_cast<MprpcController *>(controller);

  // 组织请求包：service_name + method_name + args + msg_no
  crpc::RpcStruct pb_struct;
  pb_struct.service_name = service_name;
  pb_struct.method_name = method_name;
  pb_struct.pb_data = args_str;
  pb_struct.msg_no = crpc::MsgReqUtil::genMsgNumber();

  crpc::NetAddress::ptr addr = std::make_shared<crpc::IPAddress>(ip, port);
  crpc::TcpClient::ptr client = std::make_shared<crpc::TcpClient>(addr);
  client->setTimeout(rpc_controller ? rpc_controller->Timeout() : 5000);

  // 把请求编码进连接发送缓冲
  client->getConnection()->getCodec()->encode(client->getConnection()->getOutBuffer(), &pb_struct);
  if (!pb_struct.encode_succ)
  {
    controller->SetFailed("encode request error");
    return;
  }

  // 协程化发送与接收（在协程中会 yield 让出，在main线程则退化为阻塞IO）
  crpc::RpcStruct::ptr res_data;
  int rt = client->sendAndRecv(pb_struct.msg_no, res_data);
  if (rt != 0)
  {
    if (rpc_controller)
    {
      rpc_controller->SetError(rt, client->getErrInfo());
    }
    else
    {
      controller->SetFailed(client->getErrInfo());
    }
    return;
  }

  if (res_data->err_code != 0)
  {
    if (rpc_controller)
    {
      rpc_controller->SetError(res_data->err_code, res_data->err_info);
    }
    else
    {
      controller->SetFailed(res_data->err_info);
    }
    return;
  }

  if (!response->ParseFromString(res_data->pb_data))
  {
    if (rpc_controller)
    {
      rpc_controller->SetError(crpc::ERROR_FAILED_DESERIALIZE, "deserialize response error");
    }
    else
    {
      controller->SetFailed("deserialize response error");
    }
    return;
  }
}
