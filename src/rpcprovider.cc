#include "rpcprovider.h"
#include <string>
#include <functional>
#include <google/protobuf/descriptor.h>
#include "mprpcapplication.h"
#include "rpcheader.pb.h"
#include "zookeeperutil.h"
// 这里是框架提供给外部使用 可以发布给rpc方法的函数接口
/*
m_serviceMap  // 第一级：服务名 → ServiceInfo
│
└─ ServiceInfo                      // 一个服务 = 两样东西
   ├─ m_service   : Service*                    ← 能跑的实现对象（UserService）
   └─ m_methodMap : map<string, MethodDescriptor*>   ← 方法名 → 方法描述
                        │
                        └─ 第二级：method_name → MethodDescriptor
*/
void RpcProvider::NotifyService(google::protobuf::Service *service)
{
  ServiceInfo service_info;

  const google::protobuf::ServiceDescriptor *pserviceDesc = service->GetDescriptor();
  std::string service_name = pserviceDesc->name();
  int methodCnt = pserviceDesc->method_count();

  LOG_INFO("service_name:%s", service_name.c_str());
  for (int i = 0; i < methodCnt; ++i)
  {
    // 获取了服务对象指定下标的服务方法的描述（抽象描述） UserService   Login
    const google::protobuf::MethodDescriptor *pmethodDesc = pserviceDesc->method(i);
    std::string method_name = pmethodDesc->name();
    service_info.m_methodMap.insert({method_name, pmethodDesc});

    // std::cout << "method name: " << method_name << std::endl;
    LOG_INFO("method_name:%s", method_name.c_str());
  }
  service_info.m_service = service;
  m_serviceMap.insert({service_name, service_info});
}

// 启动rpc服务节点 开始提供rpc服务
void RpcProvider::Run()
{
  std::string ip = MprpcApplication::GetConfig().Load("rpcserverip");
  uint16_t port = atoi(MprpcApplication::GetConfig().Load("rpcserverport").c_str());
  muduo::net::InetAddress address(ip, port);

  // create muduo server
  muduo::net::TcpServer server(&m_eventLoop, address, "RpcProvider");
  // bind connect fun / callback fun
  server.setConnectionCallback(std::bind(&RpcProvider::OnConnection, this, std::placeholders::_1));
  server.setMessageCallback(std::bind(&RpcProvider::OnMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
  // set muduo thread number
  server.setThreadNum(4);
  // 把当前rpc节点上要发布的服务全部注册到zk上面，让rpc client可以从zk上发现服务
  // session timeout   30s     zkclient 网络I/O线程  1/3 * timeout 时间发送ping消息
  ZkClient zkCli;
  zkCli.Start();
  // service_name为永久性节点    method_name为临时性节点
  for (auto &sp : m_serviceMap)
  {
    // /service_name   /UserServiceRpc
    std::string service_path = "/" + sp.first;
    zkCli.Create(service_path.c_str(), nullptr, 0);
    for (auto &mp : sp.second.m_methodMap)
    {
      // /service_name/method_name   /UserServiceRpc/Login 存储当前这个rpc服务节点主机的ip和port
      std::string method_path = service_path + "/" + mp.first;
      char method_path_data[128] = {0};
      sprintf(method_path_data, "%s:%d", ip.c_str(), port);
      // ZOO_EPHEMERAL表示znode是一个临时性节点
      zkCli.Create(method_path.c_str(), method_path_data, strlen(method_path_data), ZOO_EPHEMERAL);
    }
  }
  // std::cout << "RpcProvider start service at ip:" << ip << " port:" << port << std::endl;
  LOG_INFO("RpcProvider start service at ip:%s port: %d", ip.c_str(), port);
  // start net server
  server.start();
  m_eventLoop.loop();
}

void RpcProvider::OnConnection(const muduo::net::TcpConnectionPtr &conn)
{
  if (!conn->connected())
  {
    // 和rpc client的连接断开了
    conn->shutdown();
  }
}

/*
在框架内部，RpcProvider和RpcConsumer协商好之间通信用的protobuf数据类型
service_name method_name args    定义proto的message类型，进行数据头的序列化和反序列化
                                 service_name method_name args_size
header_size(4个字节) + header_str + args_str

┌─────────────┬──────────────────────────────────┬─────────────────────────────┐
│ 第1段        │ 第2段                             │ 第3段                        │
│ header_size │ RpcHeader                         │ args                        │
│ 4 字节       │ 25 字节                            │ 19 字节                     │
├─────────────┼──────────────────────────────────┼─────────────────────────────┤
│ 数字 25 的   │ 又拆成3小块：                       │ LoginRequest 序列化后的结果： │
│ 二进制形式    │ ┌ service_name:"UserServiceRpc"   │ ┌ name:"zhang san"          │
│ 0x19 00 00 00│ ├ method_name :"Login"            │ └ pwd :"123456"             │
│             │ └ args_size  :19                  │                             │
├─────────────┼──────────────────────────────────┼─────────────────────────────┤
*/
void RpcProvider::OnMessage(const muduo::net::TcpConnectionPtr &conn,
                            muduo::net::Buffer *buffer,
                            muduo::Timestamp)
{
  // 网络上接收的远程rpc调用请求的字符流 Login args
  std::string recv_buf = buffer->retrieveAllAsString();

  uint32_t header_size = 0;
  recv_buf.copy((char *)&header_size, 4, 0);

  // 读取header_size读取数据的原始字节流
  std::string rpc_header_str = recv_buf.substr(4, header_size);

  mprpc::RpcHeader rpcHeader;
  std::string service_name;
  std::string method_name;
  uint32_t args_size;
  if (rpcHeader.ParseFromString(rpc_header_str)) // 反序列化成功
  {
    service_name = rpcHeader.service_name();
    method_name = rpcHeader.method_name();
    args_size = rpcHeader.args_size();
  }
  else
  {
    std::cout << "rpc_header_str" << rpc_header_str << "parse error!" << std::endl;
    return;
  }

  // 获取rpc方法参数的字符流数据
  std::string args_str = recv_buf.substr(4 + header_size, args_size);

  // 打印调试信息
  std::cout << "============================================" << std::endl;
  std::cout << "header_size: " << header_size << std::endl;
  std::cout << "rpc_header_str: " << rpc_header_str << std::endl;
  std::cout << "service_name: " << service_name << std::endl;
  std::cout << "method_name: " << method_name << std::endl;
  std::cout << "args_str: " << args_str << std::endl;
  std::cout << "============================================" << std::endl;

  // 获取service对象和method对象
  auto it = m_serviceMap.find(service_name);
  if (it == m_serviceMap.end())
  {
    std::cout << service_name << " is not exist!" << std::endl;
    return;
  }

  auto mit = it->second.m_methodMap.find(method_name);
  if (mit == it->second.m_methodMap.end())
  {
    std::cout << service_name << ":" << method_name << " is not exist!" << std::endl;
    return;
  }

  google::protobuf::Service *service = it->second.m_service;      // 获取service对象  new UserService
  const google::protobuf::MethodDescriptor *method = mit->second; // 获取method对象  Login

  // 生成rpc方法调用的请求request和响应response参数
  google::protobuf::Message *request = service->GetRequestPrototype(method).New();
  if (!request->ParseFromString(args_str))
  {
    std::cout << "request parse error, content:" << args_str << std::endl;
    return;
  }
  google::protobuf::Message *response = service->GetResponsePrototype(method).New();

  // 给下面的method方法的调用，绑定一个Closure的回调函数
  google::protobuf::Closure *done = google::protobuf::NewCallback<RpcProvider,
                                                                  const muduo::net::TcpConnectionPtr &,
                                                                  google::protobuf::Message *>(this,
                                                                                               &RpcProvider::SendRpcResponse,
                                                                                               conn, response);

  // 在框架上根据远端rpc请求，调用当前rpc节点上发布的方法
  // new UserService().Login(controller, request, response, done)
  service->CallMethod(method, nullptr, request, response, done);
}

// Closure的回调操作，用于序列化rpc的响应和网络发送
void RpcProvider::SendRpcResponse(const muduo::net::TcpConnectionPtr &conn, google::protobuf::Message *response)
{
  std::string response_str;
  if (response->SerializeToString(&response_str)) // response进行序列化
  {
    // 序列化成功后，通过网络把rpc方法执行的结果发送会rpc的调用方
    conn->send(response_str);
  }
  else
  {
    std::cout << "serialize response_str error!" << std::endl;
  }
  conn->shutdown(); // 模拟http的短链接服务，由rpcprovider主动断开连接
}