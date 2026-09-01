#pragma once

#include <map>
#include <memory>
#include <string>

#include "net/abstract_dispatcher.h"
#include "net/http/http_servlet.h"

namespace crpc {

class HttpDispatcher : public AbstractDispatcher {
 public:
  HttpDispatcher() = default;
  ~HttpDispatcher() override = default;

  void dispatch(AbstractData* data, TcpConnection* connection) override;
  bool registerServlet(const std::string& path, HttpServlet::ptr servlet);

 private:
  std::map<std::string, HttpServlet::ptr> m_servlets;
};

}  // namespace crpc
