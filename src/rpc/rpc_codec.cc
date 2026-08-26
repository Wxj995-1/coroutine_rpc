#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include "rpc/rpc_codec.h"
#include "rpc/rpc_data.h"
#include "net/byte.h"
#include "comm/log.h"
#include "comm/msg_req.h"
#include "../rpcheader.pb.h"

namespace crpc {

RpcCodeC::RpcCodeC() {}

RpcCodeC::~RpcCodeC() {}

ProtocalType RpcCodeC::getProtocalType() {
  return TinyPb_Protocal;
}

void RpcCodeC::encode(TcpBuffer* buf, AbstractData* data) {
  if (!buf || !data) {
    ErrorLog << "encode error! buf or data nullptr";
    return;
  }
  RpcStruct* tmp = dynamic_cast<RpcStruct*>(data);

  int len = 0;
  const char* re = encodePbData(tmp, len);
  if (re == nullptr || len == 0) {
    ErrorLog << "encode error";
    data->encode_succ = false;
    return;
  }
  if (buf != nullptr) {
    buf->writeToBuffer(re, len);
  }
  data = tmp;
  if (re) {
    free((void*)re);
    re = NULL;
  }
}

const char* RpcCodeC::encodePbData(RpcStruct* data, int& len) {
  if (data->service_name.empty() || data->method_name.empty()) {
    ErrorLog << "encode error, service_name or method_name is empty";
    data->encode_succ = false;
    return nullptr;
  }
  if (data->msg_no.empty()) {
    data->msg_no = MsgReqUtil::genMsgNumber();
  }

  mprpc::RpcHeader header;
  header.set_service_name(data->service_name);
  header.set_method_name(data->method_name);
  header.set_args_size(data->pb_data.size());
  std::string header_str;
  if (!header.SerializeToString(&header_str)) {
    ErrorLog << "serialize rpc header error";
    data->encode_succ = false;
    return nullptr;
  }

  uint32_t msg_no_len = data->msg_no.size();
  uint32_t header_size = header_str.size();

  int pk_len = 4 + msg_no_len + 4 + header_size + data->pb_data.size();
  char* buf = reinterpret_cast<char*>(malloc(pk_len));
  char* tmp = buf;

  uint32_t net = htonl(msg_no_len);
  memcpy(tmp, &net, sizeof(uint32_t));
  tmp += sizeof(uint32_t);

  memcpy(tmp, data->msg_no.data(), msg_no_len);
  tmp += msg_no_len;

  net = htonl(header_size);
  memcpy(tmp, &net, sizeof(uint32_t));
  tmp += sizeof(uint32_t);

  memcpy(tmp, header_str.data(), header_size);
  tmp += header_size;

  memcpy(tmp, data->pb_data.data(), data->pb_data.size());
  tmp += data->pb_data.size();

  data->args_size = data->pb_data.size();
  data->encode_succ = true;
  len = pk_len;
  return buf;
}

void RpcCodeC::decode(TcpBuffer* buf, AbstractData* data) {
  if (!buf || !data) {
    ErrorLog << "decode error! buf or data nullptr";
    return;
  }

  RpcStruct* pb_struct = dynamic_cast<RpcStruct*>(data);
  pb_struct->decode_succ = false;

  int total = buf->readAble();
  int read_index = buf->readIndex();
  std::vector<char>& buffer = buf->m_buffer;

  if (total < 4) {
    DebugLog << "not enough data for msg_no_len";
    return;
  }

  uint32_t msg_no_len = getInt32FromNetByte(&buffer[read_index]);
  if (total < 4 + (int)msg_no_len + 4) {
    DebugLog << "not enough data for msg_no + header_size";
    return;
  }

  pb_struct->msg_no.assign(&buffer[read_index + 4], msg_no_len);

  uint32_t header_size = getInt32FromNetByte(&buffer[read_index + 4 + msg_no_len]);
  if (total < 4 + (int)msg_no_len + 4 + (int)header_size) {
    DebugLog << "not enough data for header";
    return;
  }

  mprpc::RpcHeader header;
  std::string header_str(&buffer[read_index + 4 + msg_no_len + 4], header_size);
  if (!header.ParseFromString(header_str)) {
    ErrorLog << "parse rpc header error";
    return;
  }

  pb_struct->service_name = header.service_name();
  pb_struct->method_name = header.method_name();
  pb_struct->args_size = header.args_size();

  int args_start = read_index + 4 + msg_no_len + 4 + header_size;
  if (total < args_start + (int)pb_struct->args_size) {
    DebugLog << "not enough data for args";
    return;
  }

  pb_struct->pb_data.assign(&buffer[args_start], pb_struct->args_size);

  int consume = 4 + msg_no_len + 4 + header_size + pb_struct->args_size;
  buf->recycleRead(consume);

  pb_struct->decode_succ = true;
  data = pb_struct;
}

}  // namespace crpc
