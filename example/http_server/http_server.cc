#include <memory>
#include <string>

#include "application/rpcapplication.h"
#include "application/rpcruntime.h"
#include "comm/config.h"
#include "comm/log.h"
#include "net/http/http_define.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "net/http/http_servlet.h"
#include "net/net_address.h"
#include "net/tcp/tcp_server.h"

namespace {

class HelloServlet : public crpc::HttpServlet {
 public:
  void handle(crpc::HttpRequest* request,
              crpc::HttpResponse* response) override {
    setHttpCode(response, crpc::HTTP_OK);
    setHttpContentType(response, "text/plain;charset=utf-8");

    std::string name = "world";
    const auto it = request->m_query_maps.find("name");
    if (it != request->m_query_maps.end() && !it->second.empty()) {
      name = it->second;
    }
    setHttpBody(response, "hello " + name + "\n");
  }

  std::string getServletName() override {
    return "HelloServlet";
  }
};

class EchoServlet : public crpc::HttpServlet {
 public:
  void handle(crpc::HttpRequest* request,
              crpc::HttpResponse* response) override {
    setHttpCode(response, crpc::HTTP_OK);
    setHttpContentType(response, "text/plain;charset=utf-8");
    setHttpBody(response, request->m_request_body);
  }

  std::string getServletName() override {
    return "EchoServlet";
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
          "/hello", std::make_shared<HelloServlet>()) ||
      !server->registerHttpServlet(
          "/echo", std::make_shared<EchoServlet>())) {
    ErrorLog << "event=http_server_start_failed"
             << " reason=servlet_registration_failed";
    crpc::ShutdownLogger();
    return 1;
  }

  crpc::SetRpcServer(server);
  InfoLog << "event=http_server_starting listen=" << address->toString();
  server->start();
  crpc::SetRpcServer(nullptr);
  crpc::ShutdownLogger();
  return 0;
}
