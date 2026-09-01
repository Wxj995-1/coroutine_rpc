#pragma once

#include <map>
#include <memory>
#include <string>

#include "net/abstract_data.h"
#include "net/http/http_define.h"

namespace crpc {

class HttpRequest : public AbstractData {
 public:
  typedef std::shared_ptr<HttpRequest> ptr;

 public:
  HttpMethod m_request_method {HTTP_METHOD_UNKNOWN};
  std::string m_request_path {"/"};
  std::string m_request_query;
  std::string m_request_version {"HTTP/1.1"};
  HttpRequestHeader m_request_header;
  std::string m_request_body;
  std::map<std::string, std::string> m_query_maps;
};

}  // namespace crpc
