#include <cassert>
#include <iostream>
#include <string>

#include "net/error_code.h"
#include "net/tcp/tcp_buffer.h"
#include "rpc/rpc_codec.h"
#include "rpc/rpc_data.h"

namespace {

crpc::RpcStruct MakePacket(const std::string& msg_no,
                           const std::string& method_name,
                           const std::string& payload, int32_t err_code = 0,
                           const std::string& err_info = "") {
  crpc::RpcStruct packet;
  packet.msg_no = msg_no;
  packet.service_name = "FriendService";
  packet.method_name = method_name;
  packet.pb_data = payload;
  packet.err_code = err_code;
  packet.err_info = err_info;
  return packet;
}

void AssertPacketEquals(const crpc::RpcStruct& actual,
                        const crpc::RpcStruct& expected) {
  assert(actual.msg_no == expected.msg_no);
  assert(actual.service_name == expected.service_name);
  assert(actual.method_name == expected.method_name);
  assert(actual.pb_data == expected.pb_data);
  assert(actual.err_code == expected.err_code);
  assert(actual.err_info == expected.err_info);
}

void TestRoundTrip() {
  crpc::RpcCodeC codec;
  crpc::TcpBuffer buffer(16);
  crpc::RpcStruct source =
      MakePacket("10001", "GetFriendsList", "protobuf-payload");

  codec.encode(&buffer, &source);
  assert(source.encode_succ);

  crpc::RpcStruct decoded;
  codec.decode(&buffer, &decoded);
  assert(decoded.decode_succ);
  AssertPacketEquals(decoded, source);
  assert(buffer.readAble() == 0);
}

void TestErrorRoundTrip() {
  crpc::RpcCodeC codec;
  crpc::TcpBuffer buffer(16);
  crpc::RpcStruct source =
      MakePacket("10002", "MissingMethod", "", crpc::ERROR_METHOD_NOT_FOUND,
                 "not found method_name:[MissingMethod]");

  codec.encode(&buffer, &source);
  assert(source.encode_succ);

  crpc::RpcStruct decoded;
  codec.decode(&buffer, &decoded);
  assert(decoded.decode_succ);
  AssertPacketEquals(decoded, source);
}

void TestHalfPacket() {
  crpc::RpcCodeC codec;
  crpc::TcpBuffer encoded(16);
  crpc::RpcStruct source =
      MakePacket("10003", "GetFriendsList", "split-payload");
  codec.encode(&encoded, &source);
  assert(source.encode_succ);

  const std::string bytes = encoded.getBufferString();
  const std::size_t split = bytes.size() / 2;
  crpc::TcpBuffer received(8);
  received.writeToBuffer(bytes.data(), static_cast<int>(split));

  crpc::RpcStruct partial;
  codec.decode(&received, &partial);
  assert(!partial.decode_succ);
  assert(received.readAble() == static_cast<int>(split));

  received.writeToBuffer(bytes.data() + split,
                         static_cast<int>(bytes.size() - split));
  crpc::RpcStruct decoded;
  codec.decode(&received, &decoded);
  assert(decoded.decode_succ);
  AssertPacketEquals(decoded, source);
}

void TestStickyPackets() {
  crpc::RpcCodeC codec;
  crpc::TcpBuffer buffer(16);
  crpc::RpcStruct first = MakePacket("10004", "MethodOne", "first");
  crpc::RpcStruct second = MakePacket("10005", "MethodTwo", "second");

  codec.encode(&buffer, &first);
  codec.encode(&buffer, &second);
  assert(first.encode_succ && second.encode_succ);

  crpc::RpcStruct first_decoded;
  codec.decode(&buffer, &first_decoded);
  assert(first_decoded.decode_succ);
  AssertPacketEquals(first_decoded, first);

  crpc::RpcStruct second_decoded;
  codec.decode(&buffer, &second_decoded);
  assert(second_decoded.decode_succ);
  AssertPacketEquals(second_decoded, second);
  assert(buffer.readAble() == 0);
}

}  // namespace

int main() {
  TestRoundTrip();
  TestErrorRoundTrip();
  TestHalfPacket();
  TestStickyPackets();
  std::cout << "rpc codec tests passed" << std::endl;
  return 0;
}
