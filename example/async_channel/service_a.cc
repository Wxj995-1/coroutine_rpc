#include <memory>
#include <string>

#include "application/rpcapplication.h"
#include "async_example.pb.h"
#include "comm/log.h"
#include "rpc/rpc_closure.h"
#include "rpc/rpcasyncchannel.h"
#include "rpc/rpccontroller.h"
#include "rpc/rpcprovider.h"

class OrderServiceImpl : public asyncdemo::OrderService {
 public:
  void CreateOrder(google::protobuf::RpcController* controller,
                   const asyncdemo::CreateOrderRequest* request,
                   asyncdemo::CreateOrderResponse* response,
                   google::protobuf::Closure* done) override {
    (void)controller;

    auto channel = std::make_shared<RpcAsyncChannel>();
    auto async_controller = std::make_shared<RpcController>();
    auto verify_request = std::make_shared<asyncdemo::VerifyRequest>();
    auto verify_response = std::make_shared<asyncdemo::VerifyResponse>();

    verify_request->set_user_name(request->user_name());
    verify_request->set_password(request->password());

    const std::string user_name = request->user_name();
    auto closure = std::make_shared<crpc::RpcClosure>([user_name]() {
      AppInfoLog("service A received async callback from service B, user=%s",
                 user_name.c_str());
    });

    channel->saveCallee(async_controller, verify_request, verify_response,
                        closure);
    asyncdemo::AuthService_Stub auth_stub(channel.get());
    auth_stub.Verify(async_controller.get(), verify_request.get(),
                     verify_response.get(), nullptr);

    // A 的响应依赖 B 的认证结果。wait() 只挂起当前请求协程，
    // 不会阻塞 A 所在的 IO 线程。
    channel->wait();

    if (async_controller->Failed()) {
      const int error_code = async_controller->ErrorCode();
      response->set_code(error_code == 0 ? -1 : error_code);
      response->set_info(async_controller->ErrorText());
    } else if (!verify_response->passed()) {
      response->set_code(verify_response->code());
      response->set_info(verify_response->info());
    } else {
      response->set_code(0);
      response->set_info("");
      response->set_order_id(
          "order-" + std::to_string(request->product_id()));
    }

    AppInfoLog("service A CreateOrder resumed, user=%s, code=%d",
               request->user_name().c_str(), response->code());
    done->Run();
  }
};

int main(int argc, char** argv) {
  RpcApplication::Init(argc, argv);

  RpcProvider provider;
  provider.NotifyService(new OrderServiceImpl());
  provider.Run();
  return 0;
}
