#pragma once


// RPC 框架入口，负责配置和基础组件初始化。
class RpcApplication
{
public:
  static void Init(int argc, char **argv);
  static RpcApplication &GetInstance();

private:
  RpcApplication() {}
  RpcApplication(const RpcApplication &) = delete;
  RpcApplication(RpcApplication &&) = delete;
};

using MprpcApplication = RpcApplication;
