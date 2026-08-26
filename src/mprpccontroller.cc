#include "mprpccontroller.h"

MprpcController::MprpcController()
{
  m_failed = false;
  m_errText = "";
}

void MprpcController::Reset()
{
  m_failed = false;
  m_errText = "";
  m_error_code = 0;
  m_msg_req = "";
}

bool MprpcController::Failed() const
{
  return m_failed;
}

std::string MprpcController::ErrorText() const
{
  return m_errText;
}

void MprpcController::SetFailed(const std::string &reason)
{
  m_failed = true;
  m_errText = reason;
}

void MprpcController::StartCancel() {}
bool MprpcController::IsCanceled() const { return false; }
void MprpcController::NotifyOnCancel(google::protobuf::Closure *callback) {}

int MprpcController::ErrorCode() const
{
  return m_error_code;
}

void MprpcController::SetErrorCode(int code)
{
  m_error_code = code;
}

void MprpcController::SetError(int err_code, const std::string &err_info)
{
  SetFailed(err_info);
  SetErrorCode(err_code);
}

const std::string &MprpcController::MsgSeq() const
{
  return m_msg_req;
}

void MprpcController::SetMsgReq(const std::string &msg_req)
{
  m_msg_req = msg_req;
}

void MprpcController::SetTimeout(int timeout)
{
  m_timeout = timeout;
}

int MprpcController::Timeout() const
{
  return m_timeout;
}
