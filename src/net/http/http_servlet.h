#pragma once

#include <memory>
#include <string>

#include "net/http/http_request.h"
#include "net/http/http_response.h"

namespace crpc {

class HttpServlet : public std::enable_shared_from_this<HttpServlet> {
 public:
  typedef std::shared_ptr<HttpServlet> ptr;

  HttpServlet() = default;
  virtual ~HttpServlet() = default;

  virtual void handle(HttpRequest* request, HttpResponse* response) = 0;
  virtual std::string getServletName() = 0;

  void handleNotFound(HttpRequest* request, HttpResponse* response);
  void setHttpCode(HttpResponse* response, int code);
  void setHttpContentType(HttpResponse* response,
                          const std::string& content_type);
  void setHttpBody(HttpResponse* response, const std::string& body);
  void setCommParam(HttpRequest* request, HttpResponse* response);
};

class NotFoundHttpServlet : public HttpServlet {
 public:
  NotFoundHttpServlet() = default;
  ~NotFoundHttpServlet() override = default;

  void handle(HttpRequest* request, HttpResponse* response) override;
  std::string getServletName() override;
};

}  // namespace crpc
