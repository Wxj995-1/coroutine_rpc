#pragma once
#include <google/protobuf/service.h>
#include <string>

class MprpcController : public google::protobuf::RpcController
{
public:
  MprpcController();
  void Reset();
  bool Failed() const;
  std::string ErrorText() const;
  void SetFailed(const std::string &reason);

  void StartCancel();
  bool IsCanceled() const;
  void NotifyOnCancel(google::protobuf::Closure *callback);

  // 协程客户端扩展：错误码 / 请求序列号 / 超时
  int ErrorCode() const;
  void SetErrorCode(int code);
  void SetError(int err_code, const std::string &err_info);
  const std::string &MsgSeq() const;
  void SetMsgReq(const std::string &msg_req);
  void SetTimeout(int timeout);
  int Timeout() const;

private:
  bool m_failed;         // RPC方法执行过程中的状态
  std::string m_errText; // RPC方法执行过程中的错误信息
  int m_error_code {0};
  std::string m_msg_req;
  int m_timeout {5000};
};
