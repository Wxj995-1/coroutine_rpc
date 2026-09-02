# coroutine_rpc

## HTTP 性能测试

测试工具：[wrk](https://github.com/wg/wrk)

测试环境：wrk 与 `async_http_server` 部署在同一台 Linux 虚拟机，关闭框架日志，使用 `/qps` 接口。该接口只执行 HTTP 请求解析、Servlet 路由和响应编码，不调用 ZooKeeper 或下游 RPC 服务。

测试命令：

```bash
wrk -t4 -c100 -d30s --latency "http://127.0.0.1:8100/qps?id=1"
wrk -t4 -c300 -d30s --latency "http://127.0.0.1:8100/qps?id=1"
wrk -t4 -c500 -d30s --latency "http://127.0.0.1:8100/qps?id=1"
wrk -t4 -c1000 -d30s --latency "http://127.0.0.1:8100/qps?id=1"
```

测试结果：

| 并发连接数 |    请求数 |       QPS | 平均延迟 |   P50 |      P90 |       P99 | Timeout |
| ---------: | --------: | --------: | -------: | ----: | -------: | --------: | ------: |
|        100 | 1,964,049 | 65,430.81 |  1.63 ms | 50 us |  1.71 ms |  14.20 ms |     207 |
|        300 | 2,073,071 | 68,976.67 | 23.76 ms | 44 us |   125 us | 942.35 ms |     802 |
|        500 | 2,108,603 | 70,065.59 | 50.96 ms | 65 us | 41.46 ms |    1.15 s |     443 |
|       1000 | 2,017,110 | 67,122.89 | 29.00 ms | 63 us | 11.24 ms | 861.14 ms |     834 |

本轮测试在 500 并发时取得最高约 7 万 QPS；从 300 并发开始吞吐增长趋缓，1000 并发时 QPS 回落，说明当前环境下吞吐拐点约在 300 ～ 500 并发。
基于协程 Reactor 网络层、protobuf 和 ZooKeeper 的 RPC 框架。

## 目录

```text
config/              运行配置
proto/               protobuf 源文件
src/application/     框架初始化入口
src/comm/            配置、日志和通用组件
src/coroutine/       协程与 hook
src/net/             Reactor 与 TCP 网络层
src/registry/        ZooKeeper 注册中心
src/rpc/             RPC Channel、Provider、编解码与分发
example/generated/   示例协议生成代码
example/callee/      服务提供方示例
example/caller/      服务调用方示例
```

项目不再使用单独的 `include/` 目录。头文件与对应实现放在同一功能模块中，引用路径从 `src/` 或 `example/` 开始。

## RPC 线协议

框架头采用 TinyPB 风格的固定顺序二进制协议，业务请求和响应正文仍使用 protobuf：

```text
[START][packet_len][msg_no_len][msg_no]
[service_full_name_len][service_full_name]
[err_code][err_info_len][err_info][pb_data][checksum][END]
```

`err_code` 和 `err_info` 会随响应包传输到客户端；原先用于框架头的 `RpcHeader.proto` 已移除。

## 异步 RPC Channel

`RpcAsyncChannel` 参考 TinyRPC 的实现：在当前服务协程中发起调用，把实际 RPC 投递到其他 IO 线程，完成后再切回发起调用的 IO 线程。请求对象、响应对象、控制器和回调必须使用 `shared_ptr`，并在调用 stub 前通过 `saveCallee` 保存生命周期：

```cpp
#include "rpc/rpc_closure.h"
#include "rpc/rpcasyncchannel.h"
#include "rpc/rpccontroller.h"

auto channel = std::make_shared<RpcAsyncChannel>();
auto controller = std::make_shared<RpcController>();
auto request = std::make_shared<fixbug::LoginRequest>();
auto response = std::make_shared<fixbug::LoginResponse>();
auto closure = std::make_shared<crpc::RpcClosure>([]() {
  // RPC 完成后在发起调用的 IO 线程执行。
});

channel->saveCallee(controller, request, response, closure);
fixbug::UserServiceRpc_Stub stub(channel.get());
stub.Login(controller.get(), request.get(), response.get(), nullptr);

// 当前服务的响应依赖这次 RPC 结果时，在返回服务方法前等待。
channel->wait();
if (controller->Failed()) {
  ErrorLog << controller->ErrorText();
}
```

当前实现的使用边界：

- 必须由 `RpcProvider` 的 IO 协程发起，不能在普通 `main` 线程中直接使用。
- 调用 stub 前必须先调用 `saveCallee`，并用 `std::make_shared<RpcAsyncChannel>()` 创建 Channel。
- 如果当前服务响应依赖异步调用结果，必须在服务方法返回前调用 `wait()`；当前 dispatcher 仍在服务方法返回后立即编码响应。
- 回调请使用 `crpc::RpcClosure`，不要把 protobuf 的自删除 `NewCallback` 直接交给 `shared_ptr` 管理。

### 异步示例

`example/async_channel` 使用三个独立进程验证完整链路：

```text
Consumer --CreateOrder--> 服务 A --RpcAsyncChannel::Verify--> 服务 B
Consumer <--订单结果------ 服务 A <----------认证结果---------- 服务 B
```

Consumer 会执行两个测试：

1. `A to B success`：服务 B 认证成功，服务 A 等待异步结果后创建订单。
2. `A to B business failure`：异步 RPC 正常完成，但服务 B 返回密码错误，服务 A 将业务错误返回 Consumer。

先启动 ZooKeeper，再按顺序打开三个终端运行：

```bash
./build/bin/async_service_b -i config/async_service_b.conf
./build/bin/async_service_a -i config/async_service_a.conf
./build/bin/async_consumer -i config/async_consumer.conf
```

服务 B 日志会出现 `Verify finished`；服务 A 日志会依次出现异步回调和 `CreateOrder resumed`。

## 构建

```bash
bash autobuild.sh
ctest --test-dir build --output-on-failure
```

构建结果位于：

```text
build/bin/provider
build/bin/consumer
build/lib/libmprpc.a
```

## 运行

先启动 ZooKeeper，然后在项目根目录运行：

```bash
./build/bin/provider -i config/test.conf
./build/bin/consumer -i config/test.conf
```

## 日志配置

日志系统在 `RpcApplication::Init` 加载配置后自动启动。框架日志使用 `DebugLog`、`InfoLog`、`WarnLog`、`ErrorLog`，业务日志使用 `AppDebugLog`、`AppInfoLog`、`AppWarnLog`、`AppErrorLog`。

```ini
log_path=./log/
log_prefix=coroutine_rpc
log_max_file_size=5
rpc_log_level=DEBUG
app_log_level=DEBUG
log_sync_interval=500
```

`log_max_file_size` 的单位是 MB，`log_sync_interval` 的单位是毫秒。RPC 日志和业务日志分别写入独立文件，并按日期和文件大小轮转。

Linux 进程信号由框架初始化时统一配置：忽略 `SIGPIPE`；`SIGINT` 和 `SIGTERM` 由专用 `sigwait` 线程接收，并在退出前调用 `ShutdownLogger()` 排空日志；`SIGSEGV` 和 `SIGABRT` 只向标准错误输出固定提示，随后恢复默认信号动作以保留 core dump，致命信号处理器不会调用异步日志或等待后台线程。
