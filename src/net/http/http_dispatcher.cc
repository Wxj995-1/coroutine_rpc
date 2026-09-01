#include "net/http/http_dispatcher.h"

#include "comm/log.h"
#include "comm/msg_req.h"
#include "coroutine/coroutine.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "net/tcp/tcp_connection.h"

namespace crpc {
namespace {

std::string NormalizePath(const std::string& path) {
  if (path.empty()) {
    return "/";
  }
  return path.front() == '/' ? path : "/" + path;
}

}  // namespace

void HttpDispatcher::dispatch(AbstractData* data,
                              TcpConnection* connection) {
  HttpRequest* request = dynamic_cast<HttpRequest*>(data);
  if (request == nullptr || connection == nullptr) {
    ErrorLog << "event=http_dispatch_failed reason=invalid_argument";
    return;
  }

  RunTime* runtime = Coroutine::GetCurrentCoroutine()->getRunTime();
  runtime->m_msg_no = MsgReqUtil::genMsgNumber();
  setCurrentRunTime(runtime);

  const std::string path = NormalizePath(request->m_request_path);
  HttpServlet::ptr servlet;
  const auto it = m_servlets.find(path);
  if (it == m_servlets.end()) {
    servlet = std::make_shared<NotFoundHttpServlet>();
  } else {
    servlet = it->second;
  }

  runtime->m_interface_name = servlet->getServletName();
  InfoLog << "event=http_dispatch_started path=" << path
          << " method=" << (request->m_request_method == GET ? "GET" : "POST")
          << " servlet=" << runtime->m_interface_name;

  HttpResponse response;
  servlet->setCommParam(request, &response);
  servlet->handle(request, &response);
  connection->getCodec()->encode(connection->getOutBuffer(), &response);

  InfoLog << "event=http_dispatch_completed path=" << path
          << " status=" << response.m_response_code
          << " response_body_bytes=" << response.m_response_body.size();
}

bool HttpDispatcher::registerServlet(const std::string& path,
                                     HttpServlet::ptr servlet) {
  if (!servlet) {
    ErrorLog << "event=http_servlet_registration_failed reason=null_servlet";
    return false;
  }

  const std::string normalized_path = NormalizePath(path);
  const auto result = m_servlets.emplace(normalized_path, servlet);
  if (!result.second) {
    ErrorLog << "event=http_servlet_registration_failed reason=duplicate_path"
             << " path=" << normalized_path;
    return false;
  }

  InfoLog << "event=http_servlet_registered path=" << normalized_path
          << " servlet=" << servlet->getServletName();
  return true;
}

}  // namespace crpc
