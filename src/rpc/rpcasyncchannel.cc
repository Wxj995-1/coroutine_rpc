#include "rpc/rpcasyncchannel.h"

#include <memory>
#include <utility>

#include "application/rpcruntime.h"
#include "comm/log.h"
#include "comm/msg_req.h"
#include "coroutine/coroutine_pool.h"
#include "net/error_code.h"
#include "net/reactor.h"
#include "net/tcp/tcp_server.h"
#include "rpc/rpccontroller.h"

RpcAsyncChannel::RpcAsyncChannel()
    : rpc_channel_(std::make_shared<::RpcChannel>()),
      origin_coroutine_(crpc::Coroutine::GetCurrentCoroutine()),
      origin_io_thread_(crpc::IOThread::GetCurrentIOThread()) {}

RpcAsyncChannel::~RpcAsyncChannel() {
  if (pending_coroutine_) {
    crpc::GetCoroutinePool()->returnCoroutine(pending_coroutine_);
  }
}

void RpcAsyncChannel::saveCallee(ControllerPtr controller,
                                 MessagePtr request, MessagePtr response,
                                 ClosurePtr closure) {
  controller_ = std::move(controller);
  request_ = std::move(request);
  response_ = std::move(response);
  closure_ = std::move(closure);
  prepared_ = controller_ != nullptr && request_ != nullptr &&
              response_ != nullptr;
}

void RpcAsyncChannel::setCallError(
    google::protobuf::RpcController* controller,
    const std::string& error_info) {
  RpcController* rpc_controller = dynamic_cast<RpcController*>(controller);
  if (rpc_controller != nullptr) {
    rpc_controller->SetError(crpc::ERROR_NOT_SET_ASYNC_PRE_CALL, error_info);
  } else if (controller != nullptr) {
    controller->SetFailed(error_info);
  }
  finished_ = true;
}

void RpcAsyncChannel::CallMethod(
    const google::protobuf::MethodDescriptor* method,
    google::protobuf::RpcController* controller,
    const google::protobuf::Message* request,
    google::protobuf::Message* response, google::protobuf::Closure* done) {
  (void)request;
  (void)response;
  (void)done;

  if (!prepared_) {
    const std::string error =
        "saveCallee must be called before RpcAsyncChannel::CallMethod";
    ErrorLog << error;
    setCallError(controller, error);
    return;
  }

  if (method == nullptr) {
    const std::string error = "async rpc method is null";
    ErrorLog << error;
    setCallError(controller, error);
    return;
  }

  std::shared_ptr<crpc::TcpServer> server = crpc::GetRpcServer();
  if (server == nullptr || server->getIOThreadPool() == nullptr) {
    const std::string error =
        "async rpc requires a running RpcProvider IO thread pool";
    ErrorLog << error;
    setCallError(controller, error);
    return;
  }

  if (origin_io_thread_ == nullptr || crpc::Coroutine::IsMainCoroutine()) {
    const std::string error =
        "RpcAsyncChannel must be called inside an RPC IO coroutine";
    ErrorLog << error;
    setCallError(controller, error);
    return;
  }

  RpcController* rpc_controller =
      dynamic_cast<RpcController*>(controller_.get());
  if (rpc_controller != nullptr && rpc_controller->MsgSeq().empty()) {
    crpc::RunTime* run_time = crpc::getCurrentRunTime();
    if (run_time != nullptr && !run_time->m_msg_no.empty()) {
      rpc_controller->SetMsgReq(run_time->m_msg_no);
    } else {
      rpc_controller->SetMsgReq(crpc::MsgReqUtil::genMsgNumber());
    }
  }

  std::shared_ptr<RpcAsyncChannel> self;
  try {
    self = shared_from_this();
  } catch (const std::bad_weak_ptr&) {
    const std::string error =
        "RpcAsyncChannel must be created with std::make_shared";
    ErrorLog << error;
    setCallError(controller, error);
    return;
  }

  auto rpc_task = [self, method]() mutable {
    self->rpc_channel_->CallMethod(
        method, self->controller_.get(), self->request_.get(),
        self->response_.get(), nullptr);

    // Post once on the worker reactor first. This guarantees the worker
    // coroutine has yielded before the origin thread returns it to the pool.
    crpc::IOThread* worker_thread = crpc::IOThread::GetCurrentIOThread();
    auto post_to_origin = [self]() mutable {
      auto completion = [self]() mutable {
        self->finished_ = true;

        if (self->closure_ != nullptr) {
          self->closure_->Run();
        }

        if (self->need_resume_) {
          crpc::Coroutine::Resume(self->origin_coroutine_);
        }
        self.reset();
      };

      self->origin_io_thread_->getReactor()->addTask(completion, true);
      self.reset();
    };

    worker_thread->getReactor()->addTask(post_to_origin, true);
    self.reset();
  };

  pending_coroutine_ =
      server->getIOThreadPool()->addCoroutineToRandomThread(rpc_task, false);
}

void RpcAsyncChannel::wait() {
  need_resume_ = true;
  if (finished_) {
    return;
  }

  if (origin_io_thread_ == nullptr || crpc::Coroutine::IsMainCoroutine()) {
    setCallError(controller_.get(),
                 "RpcAsyncChannel::wait cannot run in the main coroutine");
    return;
  }

  crpc::Coroutine::Yield();
}

bool RpcAsyncChannel::finished() const {
  return finished_;
}
