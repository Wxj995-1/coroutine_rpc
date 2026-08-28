#include <iostream>
#include "application/rpcapplication.h"
#include "generated/user.pb.h"
#include "rpc/rpcchannel.h"
int main(int argc, char **argv)
{
  // 使用 RPC 服务前先初始化框架。
  RpcApplication::Init(argc, argv);

  fixbug::UserServiceRpc_Stub stub(new RpcChannel());

  fixbug::LoginRequest request;
  request.set_name("zhangsan");
  request.set_pwd("123456");

  fixbug::LoginResponse response;
  // 发起调用 同步调用
  stub.Login(nullptr, &request, &response, nullptr);
  // 一次rpc调用完成，读调用的结果
  if (0 == response.result().errcode())
  {
    std::cout << "rpc login response success:" << response.sucess() << std::endl;
  }
  else
  {
    std::cout << "rpc login response error : " << response.result().errmsg() << std::endl;
  }

  // 注册远程发布的rpc方法register
  fixbug::RegisterRequest req;
  req.set_id(2000);
  req.set_name("mprpc");
  req.set_pwd("1234455");
  fixbug::RegisterResponse rsp;

  stub.Register(nullptr, &req, &rsp, nullptr);

  // 一次rpc调用完成，读调用的结果
  if (0 == rsp.result().errcode())
  {
    std::cout << "rpc login response success:" << response.sucess() << std::endl;
  }
  else
  {
    std::cout << "rpc login response error : " << response.result().errmsg() << std::endl;
  }
}
