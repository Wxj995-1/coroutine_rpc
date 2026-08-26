#pragma once

namespace crpc {

class AbstractData {
 public:
  AbstractData() = default;
  virtual ~AbstractData() {};

  bool decode_succ {false};
  bool encode_succ {false};
};

}  // namespace crpc
