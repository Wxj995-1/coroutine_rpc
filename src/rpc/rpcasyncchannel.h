#pragma once

#include <memory>
#include <string>

#include <google/protobuf/message.h>
#include <google/protobuf/service.h>

#include "coroutine/coroutine.h"
#include "net/tcp/io_thread.h"
#include "rpc/rpcchannel.h"

class RpcAsyncChannel
    : public google::protobuf::RpcChannel,
      public std::enable_shared_from_this<RpcAsyncChannel> {
 public:
  using ptr = std::shared_ptr<RpcAsyncChannel>;
  using ControllerPtr = std::shared_ptr<google::protobuf::RpcController>;
  using MessagePtr = std::shared_ptr<google::protobuf::Message>;
  using ClosurePtr = std::shared_ptr<google::protobuf::Closure>;

  RpcAsyncChannel();
  ~RpcAsyncChannel() override;

  void saveCallee(ControllerPtr controller, MessagePtr request,
                  MessagePtr response, ClosurePtr closure);

  void CallMethod(const google::protobuf::MethodDescriptor* method,
                  google::protobuf::RpcController* controller,
                  const google::protobuf::Message* request,
                  google::protobuf::Message* response,
                  google::protobuf::Closure* done) override;

  void wait();
  bool finished() const;

 private:
  void setCallError(google::protobuf::RpcController* controller,
                    const std::string& error_info);

 private:
  std::shared_ptr<::RpcChannel> rpc_channel_;
  crpc::Coroutine::ptr pending_coroutine_;
  crpc::Coroutine* origin_coroutine_ {nullptr};
  crpc::IOThread* origin_io_thread_ {nullptr};

  ControllerPtr controller_;
  MessagePtr request_;
  MessagePtr response_;
  ClosurePtr closure_;

  bool prepared_ {false};
  bool finished_ {false};
  bool need_resume_ {false};
};
