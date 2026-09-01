#include "net/http/http_servlet.h"

#include <sstream>

#include "comm/log.h"
#include "net/http/http_define.h"

namespace crpc {

void HttpServlet::handleNotFound(HttpRequest* request,
                                 HttpResponse* response) {
  if (response == nullptr) {
    return;
  }

  setHttpCode(response, HTTP_NOTFOUND);
  setHttpContentType(response, kDefaultHtmlContentType);

  std::stringstream body;
  body << "<html><body><h1>" << HTTP_NOTFOUND << ' '
       << HttpCodeToString(HTTP_NOTFOUND) << "</h1><p>Path "
       << (request == nullptr ? std::string("/") : request->m_request_path)
       << " was not found.</p></body></html>";
  setHttpBody(response, body.str());
}

void HttpServlet::setHttpCode(HttpResponse* response, int code) {
  if (response == nullptr) {
    return;
  }
  response->m_response_code = code;
  response->m_response_info = HttpCodeToString(code);
}

void HttpServlet::setHttpContentType(
    HttpResponse* response, const std::string& content_type) {
  if (response != nullptr) {
    response->m_response_header.setKeyValue("Content-Type", content_type);
  }
}

void HttpServlet::setHttpBody(HttpResponse* response,
                              const std::string& body) {
  if (response == nullptr) {
    return;
  }
  response->m_response_body = body;
  response->m_response_header.setKeyValue(
      "Content-Length", std::to_string(body.size()));
}

void HttpServlet::setCommParam(HttpRequest* request,
                               HttpResponse* response) {
  if (request == nullptr || response == nullptr) {
    return;
  }

  response->m_response_version = request->m_request_version.empty()
                                     ? "HTTP/1.1"
                                     : request->m_request_version;
  std::string connection = request->m_request_header.getValue("Connection");
  if (connection.empty()) {
    connection = response->m_response_version == "HTTP/1.0"
                     ? "close"
                     : "keep-alive";
  }
  response->m_response_header.setKeyValue("Connection", connection);
}

void NotFoundHttpServlet::handle(HttpRequest* request,
                                 HttpResponse* response) {
  DebugLog << "event=http_route_not_found path="
           << (request == nullptr ? std::string("/") : request->m_request_path);
  handleNotFound(request, response);
}

std::string NotFoundHttpServlet::getServletName() {
  return "NotFoundHttpServlet";
}

}  // namespace crpc
