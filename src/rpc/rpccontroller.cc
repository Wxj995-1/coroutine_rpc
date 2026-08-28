#include "rpc/rpccontroller.h"

RpcController::RpcController()
{
  m_failed = false;
  m_errText = "";
}

void RpcController::Reset()
{
  m_failed = false;
  m_errText = "";
  m_error_code = 0;
  m_msg_req = "";
}

bool RpcController::Failed() const
{
  return m_failed;
}

std::string RpcController::ErrorText() const
{
  return m_errText;
}

void RpcController::SetFailed(const std::string &reason)
{
  m_failed = true;
  m_errText = reason;
}

void RpcController::StartCancel() {}
bool RpcController::IsCanceled() const { return false; }
void RpcController::NotifyOnCancel(google::protobuf::Closure *callback) {}

int RpcController::ErrorCode() const
{
  return m_error_code;
}

void RpcController::SetErrorCode(int code)
{
  m_error_code = code;
}

void RpcController::SetError(int err_code, const std::string &err_info)
{
  SetFailed(err_info);
  SetErrorCode(err_code);
}

const std::string &RpcController::MsgSeq() const
{
  return m_msg_req;
}

void RpcController::SetMsgReq(const std::string &msg_req)
{
  m_msg_req = msg_req;
}

void RpcController::SetTimeout(int timeout)
{
  m_timeout = timeout;
}

int RpcController::Timeout() const
{
  return m_timeout;
}
