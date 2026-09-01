#pragma once

#include <memory>
#include <string>

#include "net/abstract_data.h"
#include "net/http/http_define.h"

namespace crpc {

class HttpResponse : public AbstractData {
 public:
  typedef std::shared_ptr<HttpResponse> ptr;

 public:
  std::string m_response_version {"HTTP/1.1"};
  int m_response_code {HTTP_OK};
  std::string m_response_info {HttpCodeToString(HTTP_OK)};
  HttpResponseHeader m_response_header;
  std::string m_response_body;
};

}  // namespace crpc
