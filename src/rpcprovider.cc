#include "rpcprovider.h"
#include <string>
#include <functional>
#include <string.h>
#include <stdlib.h>
#include <google/protobuf/descriptor.h>
#include "zookeeperutil.h"
#include "comm/config.h"
#include "comm/log.h"

void RpcProvider::NotifyService(google::protobuf::Service *service)
{
  ServiceInfo service_info;

  const google::protobuf::ServiceDescriptor *pserviceDesc = service->GetDescriptor();
  std::string service_name = pserviceDesc->name();
  int methodCnt = pserviceDesc->method_count();

  InfoLog << "service_name:" << service_name;
  for (int i = 0; i < methodCnt; ++i)
  {
    const google::protobuf::MethodDescriptor *pmethodDesc = pserviceDesc->method(i);
    std::string method_name = pmethodDesc->name();
    service_info.m_methodNames.push_back(method_name);

    InfoLog << "method_name:" << method_name;
  }
  service_info.m_service = service;
  m_serviceMap.insert({service_name, service_info});
}

// 启动rpc服务节点 开始提供rpc服务
void RpcProvider::Run()
{
  crpc::Config* config = crpc::GetConfig();
  const std::string& ip = config->m_rpc_server_ip;
  uint16_t port = config->m_rpc_server_port;
  crpc::NetAddress::ptr addr = std::make_shared<crpc::IPAddress>(ip, port);

  // 创建协程化TcpServer（主reactor accept + io线程池 + 每条连接一个协程）
  crpc::TcpServer::ptr server = std::make_shared<crpc::TcpServer>(addr);

  // 把服务注册进dispatcher
  for (auto &sp : m_serviceMap)
  {
    server->registerService(std::shared_ptr<google::protobuf::Service>(sp.second.m_service));
  }

  // 把当前rpc节点上要发布的服务全部注册到zk上面，让rpc client可以从zk上发现服务
  ZkClient zkCli;
  zkCli.Start();
  // service_name为永久性节点    method_name为临时性节点
  for (auto &sp : m_serviceMap)
  {
    std::string service_path = "/" + sp.first;
    zkCli.Create(service_path.c_str(), nullptr, 0);
    for (auto &mn : sp.second.m_methodNames)
    {
      std::string method_path = service_path + "/" + mn;
      char method_path_data[128] = {0};
      sprintf(method_path_data, "%s:%d", ip.c_str(), port);
      zkCli.Create(method_path.c_str(), method_path_data, strlen(method_path_data), ZOO_EPHEMERAL);
    }
  }

  InfoLog << "RpcProvider start service at ip:" << ip << " port: " << port;
  // start net server（阻塞在main reactor loop）
  server->start();
}
