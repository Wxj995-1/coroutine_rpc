#pragma once

#include <string>

#include "net/abstract_codec.h"
#include "net/http/http_request.h"

namespace crpc {

class HttpCodec : public AbstractCodeC {
 public:
  HttpCodec() = default;
  ~HttpCodec() override = default;

  void encode(TcpBuffer* buffer, AbstractData* data) override;
  void decode(TcpBuffer* buffer, AbstractData* data) override;
  ProtocalType getProtocalType() override;

 private:
  bool parseHttpRequestLine(HttpRequest* request, const std::string& line);
  bool parseHttpRequestHeader(HttpRequest* request,
                              const std::string& headers);
  bool parseHttpRequestContent(HttpRequest* request,
                               const std::string& content);
};

}  // namespace crpc
