#pragma once

#include <memory>

namespace crpc {

class TcpServer;

void SetRpcServer(std::shared_ptr<TcpServer> server);
std::shared_ptr<TcpServer> GetRpcServer();

}  // namespace crpc
