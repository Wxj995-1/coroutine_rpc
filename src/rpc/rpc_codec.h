#pragma once

#include <vector>

#include "net/abstract_codec.h"
#include "net/abstract_data.h"
#include "net/tcp/tcp_buffer.h"
#include "rpc/rpc_data.h"

namespace crpc {

class RpcCodeC : public AbstractCodeC {
 public:
  RpcCodeC();

  ~RpcCodeC() override;

  void encode(TcpBuffer* buf, AbstractData* data) override;

  void decode(TcpBuffer* buf, AbstractData* data) override;

  ProtocalType getProtocalType() override;

 private:
  bool encodePbData(RpcStruct* data, std::vector<char>& frame);
};

}  // namespace crpc
