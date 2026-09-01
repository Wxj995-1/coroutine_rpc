#include <unistd.h>

#include "application/rpcapplication.h"
#include "async_http_example.pb.h"
#include "comm/log.h"
#include "rpc/rpcprovider.h"

class QueryServiceImpl : public asynchttpdemo::QueryService {
 public:
  void QueryAge(google::protobuf::RpcController* controller,
                const asynchttpdemo::QueryAgeRequest* request,
                asynchttpdemo::QueryAgeResponse* response,
                google::protobuf::Closure* done) override {
    (void)controller;

    // Simulate a slow downstream operation. sleep() is coroutine-hooked, so
    // this request yields without blocking the backend IO thread.
    sleep(1);

    response->set_code(0);
    response->set_info("OK");
    response->set_id(request->id());
    response->set_age(18 + request->id() % 50);

    AppInfoLog("QueryService.QueryAge finished, id=%d, age=%d",
               response->id(), response->age());
    if (done != nullptr) {
      done->Run();
    }
  }
};

int main(int argc, char** argv) {
  RpcApplication::Init(argc, argv);

  RpcProvider provider;
  provider.NotifyService(new QueryServiceImpl());
  provider.Run();
  return 0;
}
