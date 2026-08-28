#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include "net/abstract_data.h"

namespace crpc {

// TinyPB-style wire package:
//   [START][packet_len][msg_no_len][msg_no]
//   [service_full_name_len][service_full_name]
//   [err_code][err_info_len][err_info][pb_data][checksum][END]
class RpcStruct : public AbstractData {
 public:
  typedef std::shared_ptr<RpcStruct> ptr;

  RpcStruct() = default;
  ~RpcStruct() = default;

  std::string msg_no;
  std::string service_name;
  std::string method_name;
  std::string pb_data;  // serialized request args, or serialized response

  int32_t err_code {0};
  std::string err_info;
};

}  // namespace crpc
