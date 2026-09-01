#pragma once

#include <map>
#include <string>
#include <vector>

namespace crpc {

class StringUtil {
 public:
  static void SplitStrToMap(const std::string& str,
                            const std::string& separator,
                            const std::string& joiner,
                            std::map<std::string, std::string>& result);

  static void SplitStrToVector(const std::string& str,
                               const std::string& separator,
                               std::vector<std::string>& result);
};

}  // namespace crpc
