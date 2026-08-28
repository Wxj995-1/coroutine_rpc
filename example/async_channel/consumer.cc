#include <iostream>
#include <string>

#include "application/rpcapplication.h"
#include "async_example.pb.h"
#include "rpc/rpcchannel.h"
#include "rpc/rpccontroller.h"

namespace {

void TestCreateOrder(asyncdemo::OrderService_Stub& stub,
                     const std::string& case_name,
                     const std::string& password) {
  asyncdemo::CreateOrderRequest request;
  request.set_user_name("zhangsan");
  request.set_password(password);
  request.set_product_id(2000);

  asyncdemo::CreateOrderResponse response;
  RpcController controller;
  stub.CreateOrder(&controller, &request, &response, nullptr);

  std::cout << '[' << case_name << "] ";
  if (controller.Failed()) {
    std::cout << "RPC framework error: code=" << controller.ErrorCode()
              << ", info=" << controller.ErrorText() << std::endl;
    return;
  }

  std::cout << "business code=" << response.code()
            << ", info=" << response.info()
            << ", order_id=" << response.order_id() << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
  RpcApplication::Init(argc, argv);

  RpcChannel channel;
  asyncdemo::OrderService_Stub order_stub(&channel);

  TestCreateOrder(order_stub, "A to B success", "123456");
  TestCreateOrder(order_stub, "A to B business failure", "wrong-password");
  return 0;
}
