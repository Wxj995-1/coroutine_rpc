#include "net/http/http_codec.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <vector>

#include "comm/log.h"
#include "comm/string_util.h"
#include "net/http/http_define.h"
#include "net/http/http_response.h"

namespace crpc {
namespace {

const long long kMaxHttpBodySize = 16LL * 1024 * 1024;

std::string Trim(const std::string& value) {
  const std::size_t begin = value.find_first_not_of(" \t");
  if (begin == std::string::npos) {
    return std::string();
  }
  const std::size_t end = value.find_last_not_of(" \t");
  return value.substr(begin, end - begin + 1);
}

std::string ToUpper(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::toupper(ch));
                 });
  return value;
}

std::string ToLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  return value;
}

}  // namespace

void HttpCodec::encode(TcpBuffer* buffer, AbstractData* data) {
  if (buffer == nullptr || data == nullptr) {
    ErrorLog << "event=http_encode_failed reason=null_argument";
    return;
  }

  HttpResponse* response = dynamic_cast<HttpResponse*>(data);
  if (response == nullptr) {
    ErrorLog << "event=http_encode_failed reason=invalid_response_type";
    return;
  }

  response->encode_succ = false;
  if (response->m_response_version.empty()) {
    response->m_response_version = "HTTP/1.1";
  }
  if (response->m_response_info.empty()) {
    response->m_response_info = HttpCodeToString(response->m_response_code);
  }
  response->m_response_header.setKeyValue(
      "Content-Length", std::to_string(response->m_response_body.size()));
  if (response->m_response_header.getValue("Connection").empty()) {
    response->m_response_header.setKeyValue("Connection", "keep-alive");
  }

  std::stringstream stream;
  stream << response->m_response_version << ' '
         << response->m_response_code << ' '
         << response->m_response_info << kHttpCrlf
         << response->m_response_header.toHttpString()
         << kHttpCrlf << response->m_response_body;

  const std::string encoded = stream.str();
  buffer->writeToBuffer(encoded.data(), static_cast<int>(encoded.size()));
  response->encode_succ = true;

  DebugLog << "event=http_response_encoded status="
           << response->m_response_code
           << " header_bytes="
           << response->m_response_header.getHeaderTotalLength()
           << " body_bytes=" << response->m_response_body.size()
           << " total_bytes=" << encoded.size();
}

void HttpCodec::decode(TcpBuffer* buffer, AbstractData* data) {
  if (buffer == nullptr || data == nullptr) {
    ErrorLog << "event=http_decode_failed reason=null_argument";
    return;
  }

  HttpRequest* request = dynamic_cast<HttpRequest*>(data);
  if (request == nullptr) {
    ErrorLog << "event=http_decode_failed reason=invalid_request_type";
    return;
  }
  request->decode_succ = false;

  const std::string bytes = buffer->getBufferString();
  const std::size_t request_line_end = bytes.find(kHttpCrlf);
  if (request_line_end == std::string::npos) {
    DebugLog << "event=http_request_incomplete stage=request_line"
             << " buffered_bytes=" << bytes.size();
    return;
  }

  const std::size_t header_end = bytes.find(kHttpHeaderEnd);
  if (header_end == std::string::npos) {
    DebugLog << "event=http_request_incomplete stage=headers"
             << " buffered_bytes=" << bytes.size();
    return;
  }

  if (header_end < request_line_end + kHttpCrlf.size()) {
    ErrorLog << "event=http_decode_failed stage=headers"
             << " reason=invalid_header_boundary";
    return;
  }

  if (!parseHttpRequestLine(request, bytes.substr(0, request_line_end))) {
    return;
  }

  const std::size_t headers_begin = request_line_end + kHttpCrlf.size();
  if (!parseHttpRequestHeader(
          request, bytes.substr(headers_begin, header_end - headers_begin))) {
    return;
  }

  const std::string transfer_encoding =
      ToLower(request->m_request_header.getValue("Transfer-Encoding"));
  if (!transfer_encoding.empty() && transfer_encoding != "identity") {
    ErrorLog << "event=http_decode_failed stage=headers"
             << " reason=unsupported_transfer_encoding"
             << " transfer_encoding=" << transfer_encoding;
    return;
  }

  long long content_length = 0;
  const std::string content_length_text =
      request->m_request_header.getValue("Content-Length");
  if (!content_length_text.empty()) {
    char* end = nullptr;
    content_length = std::strtoll(content_length_text.c_str(), &end, 10);
    if (end == content_length_text.c_str() || *end != '\0' ||
        content_length < 0 || content_length > kMaxHttpBodySize) {
      ErrorLog << "event=http_decode_failed stage=headers"
               << " reason=invalid_content_length value="
               << content_length_text;
      return;
    }
  }

  const std::size_t body_begin = header_end + kHttpHeaderEnd.size();
  const std::size_t total_length =
      body_begin + static_cast<std::size_t>(content_length);
  if (bytes.size() < total_length) {
    DebugLog << "event=http_request_incomplete stage=body"
             << " buffered_bytes=" << bytes.size()
             << " expected_bytes=" << total_length;
    return;
  }

  if (!parseHttpRequestContent(
          request, bytes.substr(body_begin,
                                static_cast<std::size_t>(content_length)))) {
    return;
  }

  buffer->recycleRead(static_cast<int>(total_length));
  request->decode_succ = true;

  DebugLog << "event=http_request_decoded method="
           << (request->m_request_method == GET ? "GET" : "POST")
           << " path=" << request->m_request_path
           << " query=" << request->m_request_query
           << " body_bytes=" << request->m_request_body.size()
           << " total_bytes=" << total_length;
}

bool HttpCodec::parseHttpRequestLine(HttpRequest* request,
                                     const std::string& line) {
  std::istringstream stream(line);
  std::string method;
  std::string target;
  std::string version;
  std::string extra;
  if (!(stream >> method >> target >> version) || (stream >> extra)) {
    ErrorLog << "event=http_decode_failed stage=request_line"
             << " reason=malformed_request_line";
    return false;
  }

  method = ToUpper(method);
  if (method == "GET") {
    request->m_request_method = GET;
  } else if (method == "POST") {
    request->m_request_method = POST;
  } else {
    ErrorLog << "event=http_decode_failed stage=request_line"
             << " reason=unsupported_method method=" << method;
    return false;
  }

  version = ToUpper(version);
  if (version != "HTTP/1.1" && version != "HTTP/1.0") {
    ErrorLog << "event=http_decode_failed stage=request_line"
             << " reason=unsupported_version version=" << version;
    return false;
  }
  request->m_request_version = version;

  const std::size_t scheme = target.find("://");
  if (scheme != std::string::npos) {
    const std::size_t path_begin = target.find('/', scheme + 3);
    target = path_begin == std::string::npos ? "/" : target.substr(path_begin);
  }

  const std::size_t fragment = target.find('#');
  if (fragment != std::string::npos) {
    target.erase(fragment);
  }

  const std::size_t query = target.find('?');
  request->m_request_path = query == std::string::npos
                                ? target
                                : target.substr(0, query);
  if (request->m_request_path.empty()) {
    request->m_request_path = "/";
  }
  if (request->m_request_path.front() != '/') {
    request->m_request_path.insert(request->m_request_path.begin(), '/');
  }

  if (query != std::string::npos) {
    request->m_request_query = target.substr(query + 1);
    StringUtil::SplitStrToMap(request->m_request_query, "&", "=",
                              request->m_query_maps);
  }
  return true;
}

bool HttpCodec::parseHttpRequestHeader(HttpRequest* request,
                                       const std::string& headers) {
  if (headers.empty()) {
    return true;
  }

  std::vector<std::string> lines;
  StringUtil::SplitStrToVector(headers, kHttpCrlf, lines);
  for (const std::string& line : lines) {
    const std::size_t separator = line.find(':');
    if (separator == std::string::npos || separator == 0) {
      ErrorLog << "event=http_decode_failed stage=headers"
               << " reason=malformed_header";
      return false;
    }
    const std::string key = Trim(line.substr(0, separator));
    const std::string value = Trim(line.substr(separator + 1));
    if (key.empty()) {
      ErrorLog << "event=http_decode_failed stage=headers"
               << " reason=empty_header_name";
      return false;
    }
    request->m_request_header.setKeyValue(key, value);
  }
  return true;
}

bool HttpCodec::parseHttpRequestContent(HttpRequest* request,
                                        const std::string& content) {
  request->m_request_body = content;
  return true;
}

ProtocalType HttpCodec::getProtocalType() {
  return Http_Protocal;
}

}  // namespace crpc
