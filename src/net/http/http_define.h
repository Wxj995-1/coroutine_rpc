#pragma once

#include <map>
#include <string>

namespace crpc {

extern const std::string kHttpCrlf;
extern const std::string kHttpHeaderEnd;
extern const std::string kDefaultHtmlContentType;

enum HttpMethod {
  HTTP_METHOD_UNKNOWN = 0,
  GET = 1,
  POST = 2,
};

enum HttpCode {
  HTTP_OK = 200,
  HTTP_BADREQUEST = 400,
  HTTP_FORBIDDEN = 403,
  HTTP_NOTFOUND = 404,
  HTTP_INTERNALSERVERERROR = 500,
};

const char* HttpCodeToString(int code);

class HttpHeaderComm {
 public:
  HttpHeaderComm() = default;
  virtual ~HttpHeaderComm() = default;

  int getHeaderTotalLength() const;
  std::string getValue(const std::string& key) const;
  void setKeyValue(const std::string& key, const std::string& value);
  std::string toHttpString() const;

 public:
  std::map<std::string, std::string> m_maps;
};

class HttpRequestHeader : public HttpHeaderComm {};
class HttpResponseHeader : public HttpHeaderComm {};

}  // namespace crpc
