#pragma once

#include "net/abstract_codec.h"
#include "net/abstract_data.h"
#include "net/tcp/tcp_buffer.h"
#include "rpc/rpc_data.h"

namespace crpc {

class RpcCodeC : public AbstractCodeC {
 public:
  RpcCodeC();

  ~RpcCodeC();

  void encode(TcpBuffer* buf, AbstractData* data);

  void decode(TcpBuffer* buf, AbstractData* data);

  virtual ProtocalType getProtocalType();

 private:
  const char* encodePbData(RpcStruct* data, int& len);
};

}  // namespace crpc
