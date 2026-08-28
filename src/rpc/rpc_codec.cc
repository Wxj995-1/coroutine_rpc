#include "rpc/rpc_codec.h"

#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "comm/log.h"
#include "comm/msg_req.h"
#include "rpc/rpc_data.h"

namespace crpc {
namespace {

constexpr char kPacketStart = 0x02;
constexpr char kPacketEnd = 0x03;
constexpr int32_t kChecksumPlaceholder = 1;
constexpr int32_t kMinPacketLen =
    2 * static_cast<int32_t>(sizeof(char)) +
    6 * static_cast<int32_t>(sizeof(int32_t));
constexpr int32_t kMaxPacketLen = 16 * 1024 * 1024;

void WriteInt32(char*& cursor, int32_t value) {
  uint32_t network_value = htonl(static_cast<uint32_t>(value));
  std::memcpy(cursor, &network_value, sizeof(network_value));
  cursor += sizeof(network_value);
}

void WriteString(char*& cursor, const std::string& value) {
  if (!value.empty()) {
    std::memcpy(cursor, value.data(), value.size());
    cursor += value.size();
  }
}

bool ReadInt32(const std::vector<char>& buffer, int index, int limit,
               int32_t& value) {
  if (index < 0 || limit < index ||
      limit - index < static_cast<int>(sizeof(uint32_t))) {
    return false;
  }

  uint32_t network_value = 0;
  std::memcpy(&network_value, &buffer[index], sizeof(network_value));
  value = static_cast<int32_t>(ntohl(network_value));
  return true;
}

bool ReadString(const std::vector<char>& buffer, int& cursor, int limit,
                int32_t length, std::string& value) {
  if (length < 0 || cursor < 0 || limit < cursor ||
      length > limit - cursor) {
    return false;
  }

  value.assign(&buffer[cursor], static_cast<std::size_t>(length));
  cursor += length;
  return true;
}

}  // namespace

RpcCodeC::RpcCodeC() = default;

RpcCodeC::~RpcCodeC() = default;

ProtocalType RpcCodeC::getProtocalType() {
  return TinyPb_Protocal;
}

void RpcCodeC::encode(TcpBuffer* buf, AbstractData* data) {
  if (buf == nullptr || data == nullptr) {
    ErrorLog << "encode error, buffer or data is null";
    return;
  }

  RpcStruct* rpc_data = dynamic_cast<RpcStruct*>(data);
  if (rpc_data == nullptr) {
    ErrorLog << "encode error, data is not RpcStruct";
    data->encode_succ = false;
    return;
  }

  std::vector<char> frame;
  if (!encodePbData(rpc_data, frame)) {
    return;
  }

  buf->writeToBuffer(frame.data(), static_cast<int>(frame.size()));
}

bool RpcCodeC::encodePbData(RpcStruct* data, std::vector<char>& frame) {
  data->encode_succ = false;

  if (data->service_name.empty() || data->method_name.empty()) {
    ErrorLog << "encode error, service_name or method_name is empty";
    return false;
  }

  if (data->msg_no.empty()) {
    data->msg_no = MsgReqUtil::genMsgNumber();
  }

  const std::string service_full_name =
      data->service_name + "." + data->method_name;

  const uint64_t packet_len =
      static_cast<uint64_t>(kMinPacketLen) + data->msg_no.size() +
      service_full_name.size() + data->err_info.size() +
      data->pb_data.size();

  if (packet_len > static_cast<uint64_t>(kMaxPacketLen)) {
    ErrorLog << "encode error, packet too large, size=" << packet_len;
    return false;
  }

  frame.resize(static_cast<std::size_t>(packet_len));
  char* cursor = frame.data();

  *cursor++ = kPacketStart;
  WriteInt32(cursor, static_cast<int32_t>(packet_len));

  WriteInt32(cursor, static_cast<int32_t>(data->msg_no.size()));
  WriteString(cursor, data->msg_no);

  WriteInt32(cursor, static_cast<int32_t>(service_full_name.size()));
  WriteString(cursor, service_full_name);

  WriteInt32(cursor, data->err_code);

  WriteInt32(cursor, static_cast<int32_t>(data->err_info.size()));
  WriteString(cursor, data->err_info);

  WriteString(cursor, data->pb_data);

  // Kept for TinyPB wire compatibility. This is not a real checksum yet.
  WriteInt32(cursor, kChecksumPlaceholder);
  *cursor++ = kPacketEnd;

  if (cursor != frame.data() + frame.size()) {
    ErrorLog << "encode error, calculated packet length mismatch";
    frame.clear();
    return false;
  }

  data->encode_succ = true;
  return true;
}

void RpcCodeC::decode(TcpBuffer* buf, AbstractData* data) {
  if (buf == nullptr || data == nullptr) {
    ErrorLog << "decode error, buffer or data is null";
    return;
  }

  RpcStruct* rpc_data = dynamic_cast<RpcStruct*>(data);
  if (rpc_data == nullptr) {
    ErrorLog << "decode error, data is not RpcStruct";
    return;
  }
  rpc_data->decode_succ = false;

  int read_index = buf->readIndex();
  int write_index = buf->writeIndex();
  std::vector<char>& buffer = buf->m_buffer;

  int start_index = read_index;
  while (start_index < write_index && buffer[start_index] != kPacketStart) {
    ++start_index;
  }

  if (start_index == write_index) {
    buf->recycleRead(write_index - read_index);
    ErrorLog << "decode error, packet start byte not found";
    return;
  }

  if (start_index > read_index) {
    buf->recycleRead(start_index - read_index);
    start_index = buf->readIndex();
    write_index = buf->writeIndex();
  }

  if (write_index - start_index <
      static_cast<int>(sizeof(char) + sizeof(int32_t))) {
    return;
  }

  int32_t packet_len = 0;
  if (!ReadInt32(buffer, start_index + sizeof(char), write_index, packet_len)) {
    return;
  }

  if (packet_len < kMinPacketLen || packet_len > kMaxPacketLen) {
    ErrorLog << "decode error, invalid packet length=" << packet_len;
    buf->recycleRead(1);
    return;
  }

  if (write_index - start_index < packet_len) {
    return;
  }

  const int end_index = start_index + packet_len - 1;
  if (buffer[end_index] != kPacketEnd) {
    ErrorLog << "decode error, packet end byte mismatch";
    buf->recycleRead(1);
    return;
  }

  const int checksum_index = end_index - sizeof(int32_t);
  int cursor = start_index + sizeof(char) + sizeof(int32_t);

  int32_t msg_no_len = 0;
  int32_t service_full_name_len = 0;
  int32_t err_code = 0;
  int32_t err_info_len = 0;
  std::string msg_no;
  std::string service_full_name;
  std::string err_info;

  bool valid = ReadInt32(buffer, cursor, checksum_index, msg_no_len);
  cursor += sizeof(int32_t);
  valid = valid && msg_no_len > 0 &&
          ReadString(buffer, cursor, checksum_index, msg_no_len, msg_no);

  valid = valid &&
          ReadInt32(buffer, cursor, checksum_index, service_full_name_len);
  cursor += sizeof(int32_t);
  valid = valid && service_full_name_len > 0 &&
          ReadString(buffer, cursor, checksum_index, service_full_name_len,
                     service_full_name);

  valid = valid && ReadInt32(buffer, cursor, checksum_index, err_code);
  cursor += sizeof(int32_t);

  valid = valid && ReadInt32(buffer, cursor, checksum_index, err_info_len);
  cursor += sizeof(int32_t);
  valid = valid &&
          ReadString(buffer, cursor, checksum_index, err_info_len, err_info);

  if (!valid || cursor > checksum_index) {
    ErrorLog << "decode error, invalid field length";
    buf->recycleRead(packet_len);
    return;
  }

  const std::size_t separator = service_full_name.rfind('.');
  if (separator == std::string::npos || separator == 0 ||
      separator + 1 >= service_full_name.size()) {
    ErrorLog << "decode error, invalid service full name="
             << service_full_name;
    buf->recycleRead(packet_len);
    return;
  }

  int32_t checksum = 0;
  if (!ReadInt32(buffer, checksum_index, end_index, checksum) ||
      checksum != kChecksumPlaceholder) {
    ErrorLog << "decode error, invalid checksum placeholder";
    buf->recycleRead(packet_len);
    return;
  }

  rpc_data->msg_no = std::move(msg_no);
  rpc_data->service_name = service_full_name.substr(0, separator);
  rpc_data->method_name = service_full_name.substr(separator + 1);
  rpc_data->err_code = err_code;
  rpc_data->err_info = std::move(err_info);
  rpc_data->pb_data.assign(&buffer[cursor],
                           static_cast<std::size_t>(checksum_index - cursor));

  buf->recycleRead(packet_len);
  rpc_data->decode_succ = true;
}

}  // namespace crpc
