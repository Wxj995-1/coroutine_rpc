#pragma once
#include "google/protobuf/service.h"
#include "logger.h"
#include <memory>
#include <unordered_map>
#include <muduo/net/TcpServer.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpConnection.h>
#include <muduo/net/Buffer.h>

// 框架提供的专门服务发布rpc服务的网络对象类
class RpcProvider
{
public:
  // 这里是框架提供给外部使用 可以发布给rpc方法的函数接口
  void NotifyService(google::protobuf::Service *service);

  // 启动rpc服务节点 开始提供rpc服务
  void Run();

private:
  muduo::net::EventLoop m_eventLoop;

  // service服务类型信息
  struct ServiceInfo
  {
    google::protobuf::Service *m_service;                                                    // 保存服务对象
    std::unordered_map<std::string, const google::protobuf::MethodDescriptor *> m_methodMap; // 保存服务方法
  };
  // 存储注册成功的服务对象和其服务方法的所有信息
  std::unordered_map<std::string, ServiceInfo> m_serviceMap;

  void OnConnection(const muduo::net::TcpConnectionPtr &);
  void OnMessage(const muduo::net::TcpConnectionPtr &,
                 muduo::net::Buffer *,
                 muduo::Timestamp);
  // Closure的回调操作，用于序列化rpc的响应和网络发送
  void SendRpcResponse(const muduo::net::TcpConnectionPtr &, google::protobuf::Message *);
};

/*
设计的真正目的，是让请求到来时能按名字 O(1) 查找。客户端从网络发来的是两个字符串：
OnMessage(conn, buf, ts)
 │  从网络包反序列化出: service_name="UserServiceRpc", method_name="Login"
 │
 ├─ m_serviceMap.find("UserServiceRpc")      ← 第一级查表
 │        └─ 命中 ServiceInfo
 │
 ├─ info.m_methodMap.find("Login")           ← 第二级查表
 │        └─ 命中 MethodDescriptor
 │
 ├─ service = info.m_service                 ← 拿到 UserService 对象
 ├─ request  = service->GetRequestPrototype(method).New()
 ├─ response = service->GetResponsePrototype(method).New()
 └─ service->CallMethod(method, nullptr, request, response, done)
        │
        ▼
     反射分发 → UserService::Login(controller, request, response, done)
*/