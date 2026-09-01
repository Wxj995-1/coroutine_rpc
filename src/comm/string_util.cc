#include "comm/string_util.h"

#include <utility>

namespace crpc {

void StringUtil::SplitStrToMap(
    const std::string& str, const std::string& separator,
    const std::string& joiner,
    std::map<std::string, std::string>& result) {
  if (str.empty() || separator.empty() || joiner.empty()) {
    return;
  }

  std::vector<std::string> items;
  SplitStrToVector(str, separator, items);
  for (const std::string& item : items) {
    const std::size_t position = item.find(joiner);
    if (position == std::string::npos || position == 0) {
      continue;
    }
    result[item.substr(0, position)] = item.substr(position + joiner.size());
  }
}

void StringUtil::SplitStrToVector(
    const std::string& str, const std::string& separator,
    std::vector<std::string>& result) {
  if (str.empty() || separator.empty()) {
    return;
  }

  std::size_t begin = 0;
  while (begin <= str.size()) {
    const std::size_t end = str.find(separator, begin);
    const std::size_t length =
        end == std::string::npos ? std::string::npos : end - begin;
    std::string item = str.substr(begin, length);
    if (!item.empty()) {
      result.emplace_back(std::move(item));
    }
    if (end == std::string::npos) {
      break;
    }
    begin = end + separator.size();
  }
}

}  // namespace crpc
