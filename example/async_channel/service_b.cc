#include "application/rpcapplication.h"
#include "async_example.pb.h"
#include "comm/log.h"
#include "rpc/rpcprovider.h"

class AuthServiceImpl : public asyncdemo::AuthService {
 public:
  void Verify(google::protobuf::RpcController* controller,
              const asyncdemo::VerifyRequest* request,
              asyncdemo::VerifyResponse* response,
              google::protobuf::Closure* done) override {
    (void)controller;

    const bool passed = request->password() == "123456";
    response->set_code(passed ? 0 : 1001);
    response->set_info(passed ? "" : "invalid password");
    response->set_passed(passed);

    AppInfoLog("service B Verify finished, user=%s, passed=%d",
               request->user_name().c_str(), passed);
    done->Run();
  }
};

int main(int argc, char** argv) {
  RpcApplication::Init(argc, argv);

  RpcProvider provider;
  provider.NotifyService(new AuthServiceImpl());
  provider.Run();
  return 0;
}
