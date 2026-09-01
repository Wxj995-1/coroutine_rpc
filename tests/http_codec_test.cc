#include <cassert>
#include <iostream>
#include <string>

#include "net/http/http_codec.h"
#include "net/http/http_define.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "net/tcp/tcp_buffer.h"

namespace {

void WriteString(crpc::TcpBuffer* buffer, const std::string& value) {
  buffer->writeToBuffer(value.data(), static_cast<int>(value.size()));
}

void TestGetRequest() {
  const std::string text =
      "GET /hello?name=weng HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Connection: keep-alive\r\n\r\n";

  crpc::TcpBuffer buffer(16);
  WriteString(&buffer, text);

  crpc::HttpCodec codec;
  crpc::HttpRequest request;
  codec.decode(&buffer, &request);

  assert(request.decode_succ);
  assert(request.m_request_method == crpc::GET);
  assert(request.m_request_path == "/hello");
  assert(request.m_request_query == "name=weng");
  assert(request.m_query_maps.at("name") == "weng");
  assert(request.m_request_header.getValue("host") == "localhost");
  assert(request.m_request_header.getValue("CONNECTION") == "keep-alive");
  assert(buffer.readAble() == 0);
}

void TestPostRequest() {
  const std::string body = "hello http";
  const std::string text =
      "POST /echo HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: " + std::to_string(body.size()) +
      "\r\nContent-Type: text/plain\r\n\r\n" + body;

  crpc::TcpBuffer buffer(16);
  WriteString(&buffer, text);

  crpc::HttpCodec codec;
  crpc::HttpRequest request;
  codec.decode(&buffer, &request);

  assert(request.decode_succ);
  assert(request.m_request_method == crpc::POST);
  assert(request.m_request_path == "/echo");
  assert(request.m_request_body == body);
  assert(buffer.readAble() == 0);
}

void TestPartialRequest() {
  const std::string text =
      "POST /echo HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello";
  const std::size_t split = text.size() - 2;

  crpc::TcpBuffer buffer(8);
  WriteString(&buffer, text.substr(0, split));

  crpc::HttpCodec codec;
  crpc::HttpRequest partial;
  codec.decode(&buffer, &partial);
  assert(!partial.decode_succ);
  assert(buffer.readAble() == static_cast<int>(split));

  WriteString(&buffer, text.substr(split));
  crpc::HttpRequest complete;
  codec.decode(&buffer, &complete);
  assert(complete.decode_succ);
  assert(complete.m_request_body == "hello");
  assert(buffer.readAble() == 0);
}

void TestResponseEncoding() {
  crpc::HttpResponse response;
  response.m_response_code = crpc::HTTP_OK;
  response.m_response_info = crpc::HttpCodeToString(crpc::HTTP_OK);
  response.m_response_header.setKeyValue("Content-Type", "text/plain");
  response.m_response_body = "hello";

  crpc::TcpBuffer buffer(16);
  crpc::HttpCodec codec;
  codec.encode(&buffer, &response);

  assert(response.encode_succ);
  const std::string encoded = buffer.getBufferString();
  assert(encoded.find("HTTP/1.1 200 OK\r\n") == 0);
  assert(encoded.find("content-length: 5\r\n") != std::string::npos);
  assert(encoded.find("content-type: text/plain\r\n") != std::string::npos);
  assert(encoded.substr(encoded.size() - 5) == "hello");
}

void TestUnsupportedMethod() {
  const std::string text =
      "DELETE /hello HTTP/1.1\r\nHost: localhost\r\n\r\n";
  crpc::TcpBuffer buffer(16);
  WriteString(&buffer, text);

  crpc::HttpCodec codec;
  crpc::HttpRequest request;
  codec.decode(&buffer, &request);
  assert(!request.decode_succ);
  assert(buffer.readAble() == static_cast<int>(text.size()));
}

}  // namespace

int main() {
  TestGetRequest();
  TestPostRequest();
  TestPartialRequest();
  TestResponseEncoding();
  TestUnsupportedMethod();
  std::cout << "http codec tests passed" << std::endl;
  return 0;
}
