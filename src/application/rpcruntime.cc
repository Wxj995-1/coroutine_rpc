#include "application/rpcruntime.h"

#include <utility>

#include "net/tcp/tcp_server.h"

namespace crpc {
namespace {

std::shared_ptr<TcpServer> g_rpc_server;

}  // namespace

void SetRpcServer(std::shared_ptr<TcpServer> server) {
  g_rpc_server = std::move(server);
}

std::shared_ptr<TcpServer> GetRpcServer() {
  return g_rpc_server;
}

}  // namespace crpc
