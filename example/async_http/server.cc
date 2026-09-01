#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>

#include "application/rpcapplication.h"
#include "application/rpcruntime.h"
#include "async_http_example.pb.h"
#include "comm/config.h"
#include "comm/log.h"
#include "net/http/http_define.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "net/http/http_servlet.h"
#include "net/net_address.h"
#include "net/tcp/tcp_server.h"
#include "rpc/rpc_closure.h"
#include "rpc/rpcasyncchannel.h"
#include "rpc/rpcchannel.h"
#include "rpc/rpccontroller.h"

namespace {

std::string MakeHtml(const std::string& message) {
  return "<html><body><h1>coroutine_rpc async HTTP example</h1><p>" +
         message + "</p></body></html>";
}

int GetRequestId(crpc::HttpRequest* request) {
  const auto it = request->m_query_maps.find("id");
  return it == request->m_query_maps.end() ? 0 : std::atoi(it->second.c_str());
}

class BlockCallHttpServlet : public crpc::HttpServlet {
 public:
  void handle(crpc::HttpRequest* request,
              crpc::HttpResponse* response) override {
    setHttpCode(response, crpc::HTTP_OK);
    setHttpContentType(response, crpc::kDefaultHtmlContentType);

    asynchttpdemo::QueryAgeRequest rpc_request;
    asynchttpdemo::QueryAgeResponse rpc_response;
    RpcController controller;
    controller.SetTimeout(5000);
    rpc_request.set_id(GetRequestId(request));

    AppInfoLog("BlockCallHttpServlet begins RPC, id=%d", rpc_request.id());
    RpcChannel channel;
    asynchttpdemo::QueryService_Stub stub(&channel);
    stub.QueryAge(&controller, &rpc_request, &rpc_response, nullptr);

    if (controller.Failed()) {
      setHttpCode(response, crpc::HTTP_INTERNALSERVERERROR);
      setHttpBody(response, MakeHtml("RPC failed: " +
                                     controller.ErrorText()));
      return;
    }
    if (rpc_response.code() != 0) {
      setHttpBody(response, MakeHtml("backend error: " +
                                     rpc_response.info()));
      return;
    }

    std::stringstream message;
    message << "blocking RPC succeeded, id=" << rpc_response.id()
            << ", age=" << rpc_response.age();
    setHttpBody(response, MakeHtml(message.str()));
  }

  std::string getServletName() override {
    return "BlockCallHttpServlet";
  }
};

class NonBlockCallHttpServlet : public crpc::HttpServlet {
 public:
  void handle(crpc::HttpRequest* request,
              crpc::HttpResponse* response) override {
    setHttpCode(response, crpc::HTTP_OK);
    setHttpContentType(response, crpc::kDefaultHtmlContentType);

    auto channel = std::make_shared<RpcAsyncChannel>();
    auto controller = std::make_shared<RpcController>();
    auto rpc_request =
        std::make_shared<asynchttpdemo::QueryAgeRequest>();
    auto rpc_response =
        std::make_shared<asynchttpdemo::QueryAgeResponse>();
    controller->SetTimeout(5000);
    rpc_request->set_id(GetRequestId(request));

    auto closure = std::make_shared<crpc::RpcClosure>(
        [rpc_response]() {
          AppInfoLog("NonBlockCallHttpServlet callback, id=%d, age=%d",
                     rpc_response->id(), rpc_response->age());
        });
    channel->saveCallee(controller, rpc_request, rpc_response, closure);

    AppInfoLog("NonBlockCallHttpServlet starts async RPC, id=%d",
               rpc_request->id());
    asynchttpdemo::QueryService_Stub stub(channel.get());
    stub.QueryAge(controller.get(), rpc_request.get(), rpc_response.get(),
                  nullptr);

    AppInfoLog("async RPC submitted; HTTP coroutine will wait");
    channel->wait();
    AppInfoLog("HTTP coroutine resumed after async RPC, id=%d",
               rpc_request->id());

    if (controller->Failed()) {
      setHttpCode(response, crpc::HTTP_INTERNALSERVERERROR);
      setHttpBody(response, MakeHtml("async RPC failed: " +
                                     controller->ErrorText()));
      return;
    }
    if (rpc_response->code() != 0) {
      setHttpBody(response, MakeHtml("backend error: " +
                                     rpc_response->info()));
      return;
    }

    std::stringstream message;
    message << "async RPC succeeded, id=" << rpc_response->id()
            << ", age=" << rpc_response->age();
    setHttpBody(response, MakeHtml(message.str()));
  }

  std::string getServletName() override {
    return "NonBlockCallHttpServlet";
  }
};

class QpsHttpServlet : public crpc::HttpServlet {
 public:
  void handle(crpc::HttpRequest* request,
              crpc::HttpResponse* response) override {
    setHttpCode(response, crpc::HTTP_OK);
    setHttpContentType(response, crpc::kDefaultHtmlContentType);

    std::stringstream message;
    message << "QPS echo succeeded, id=" << GetRequestId(request);
    setHttpBody(response, MakeHtml(message.str()));
  }

  std::string getServletName() override {
    return "QpsHttpServlet";
  }
};

}  // namespace

int main(int argc, char** argv) {
  RpcApplication::Init(argc, argv);

  crpc::Config* config = crpc::GetConfig();
  crpc::NetAddress::ptr address = std::make_shared<crpc::IPAddress>(
      config->m_rpc_server_ip, config->m_rpc_server_port);
  crpc::TcpServer::ptr server = std::make_shared<crpc::TcpServer>(
      address, crpc::Http_Protocal);

  if (!server->registerHttpServlet(
          "/qps", std::make_shared<QpsHttpServlet>()) ||
      !server->registerHttpServlet(
          "/block", std::make_shared<BlockCallHttpServlet>()) ||
      !server->registerHttpServlet(
          "/nonblock", std::make_shared<NonBlockCallHttpServlet>())) {
    ErrorLog << "event=async_http_server_start_failed"
             << " reason=servlet_registration_failed";
    crpc::ShutdownLogger();
    return 1;
  }

  // RpcAsyncChannel obtains its worker pool from the currently running server.
  crpc::SetRpcServer(server);
  InfoLog << "event=async_http_server_starting listen="
          << address->toString();
  server->start();
  crpc::SetRpcServer(nullptr);
  crpc::ShutdownLogger();
  return 0;
}
