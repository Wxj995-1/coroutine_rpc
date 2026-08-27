#pragma once
#include "google/protobuf/service.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "net/net_address.h"
#include "net/tcp/tcp_server.h"

// 框架提供的专门服务发布rpc服务的网络对象类
class RpcProvider
{
public:
  // 这里是框架提供给外部使用 可以发布给rpc方法的函数接口
  void NotifyService(google::protobuf::Service *service);

  // 启动rpc服务节点 开始提供rpc服务
  void Run();

private:
  // service服务类型信息
  struct ServiceInfo
  {
    google::protobuf::Service *m_service;              // 保存服务对象
    std::vector<std::string> m_methodNames;            // 保存服务方法名
  };
  // 存储注册成功的服务对象和其服务方法的所有信息（用于zk注册与dispatcher注册）
  std::unordered_map<std::string, ServiceInfo> m_serviceMap;
};
