#include "net/http/http_define.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace crpc {
namespace {

std::string NormalizeHeaderKey(const std::string& key) {
  std::string normalized = key;
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char value) {
                   return static_cast<char>(std::tolower(value));
                 });
  return normalized;
}

}  // namespace

const std::string kHttpCrlf = "\r\n";
const std::string kHttpHeaderEnd = "\r\n\r\n";
const std::string kDefaultHtmlContentType = "text/html;charset=utf-8";

const char* HttpCodeToString(int code) {
  switch (code) {
    case HTTP_OK:
      return "OK";
    case HTTP_BADREQUEST:
      return "Bad Request";
    case HTTP_FORBIDDEN:
      return "Forbidden";
    case HTTP_NOTFOUND:
      return "Not Found";
    case HTTP_INTERNALSERVERERROR:
      return "Internal Server Error";
    default:
      return "Unknown";
  }
}

std::string HttpHeaderComm::getValue(const std::string& key) const {
  const auto it = m_maps.find(NormalizeHeaderKey(key));
  return it == m_maps.end() ? std::string() : it->second;
}

int HttpHeaderComm::getHeaderTotalLength() const {
  int length = 0;
  for (const auto& item : m_maps) {
    length += static_cast<int>(item.first.size() + 2 + item.second.size() + 2);
  }
  return length;
}

void HttpHeaderComm::setKeyValue(const std::string& key,
                                 const std::string& value) {
  m_maps[NormalizeHeaderKey(key)] = value;
}

std::string HttpHeaderComm::toHttpString() const {
  std::stringstream stream;
  for (const auto& item : m_maps) {
    stream << item.first << ": " << item.second << kHttpCrlf;
  }
  return stream.str();
}

}  // namespace crpc
