#pragma once

#include <string>
#include "net/abstract_data.h"

namespace crpc {

// wire package:
//   [4 bytes msg_no_len][msg_no][4 bytes header_size][RpcHeader bytes][args]
// RpcHeader (protobuf) keeps: service_name, method_name, args_size
// msg_no is carried outside the protobuf header for request/response matching.
class RpcStruct : public AbstractData {
 public:
  typedef std::shared_ptr<RpcStruct> ptr;

  RpcStruct() = default;
  ~RpcStruct() = default;

  std::string msg_no;
  std::string service_name;
  std::string method_name;
  uint32_t args_size {0};
  std::string pb_data;  // serialized request args, or serialized response

  int32_t err_code {0};
  std::string err_info;
};

}  // namespace crpc
