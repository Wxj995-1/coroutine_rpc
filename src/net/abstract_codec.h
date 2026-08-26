#pragma once

#include <string>
#include <memory>
#include "net/tcp/tcp_buffer.h"
#include "net/abstract_data.h"

namespace crpc {

enum ProtocalType {
  TinyPb_Protocal = 1,
  Http_Protocal = 2
};

class AbstractCodeC {
 public:
  typedef std::shared_ptr<AbstractCodeC> ptr;

  AbstractCodeC() {}

  virtual ~AbstractCodeC() {}

  virtual void encode(TcpBuffer* buf, AbstractData* data) = 0;

  virtual void decode(TcpBuffer* buf, AbstractData* data) = 0;

  virtual ProtocalType getProtocalType() = 0;
};

}  // namespace crpc
