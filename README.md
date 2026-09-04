# coroutine_rpc

## 目录

- [HTTP 性能测试](#http-性能测试)
- [目录结构](#目录结构)
- [RPC 线协议](#rpc-线协议)
- [异步 RPC Channel](#异步-rpc-channel)
- [构建](#构建)
- [运行](#运行)
- [日志配置](#日志配置)
- [示例脚手架使用说明](#示例脚手架使用说明)
  - [环境要求](#环境要求)
  - [用法](#用法)
  - [生成示例](#生成示例)
  - [构建生成工程](#构建生成工程)
  - [运行生成工程](#运行生成工程)
  - [run.sh 与 shutdown.sh](#runsh-与-shutdownsh)
  - [日志位置](#日志位置)
  - [幂等与覆盖](#幂等与覆盖)
  - [实现要点与 FAQ](#实现要点与-faq)

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

## 目录结构

```text
coroutine_rpc/
├── config/                 运行配置文件（provider/consumer、async_* 各示例的 conf）
│   ├── test.conf
│   ├── async_service_a.conf / async_service_b.conf / async_consumer.conf
│   ├── async_http_server.conf / async_http_backend.conf
├── proto/                  protobuf 源文件
│   ├── user.proto / friend.proto / async_example.proto / async_http_example.proto
├── src/                    框架源码（头文件与实现同目录，不再有独立 include/）
│   ├── application/        RpcApplication 初始化与 RpcRuntime 全局单例
│   ├── comm/               配置、异步日志、信号处理与通用工具
│   ├── coroutine/          协程封装、调度池、系统调用 hook（coctx_swap.S）
│   ├── net/                Reactor / Timer / 网络地址
│   │   ├── http/           HTTP 编解码、HttpServlet、HttpDispatcher
│   │   └── tcp/            TcpServer/Client/Connection、IO 线程池、连接时间轮
│   ├── registry/           ZooKeeper 注册中心封装
│   └── rpc/                RpcChannel/AsyncChannel/Provider/Controller/Closure、
│                           协议编解码与请求分发（rpc_codec / rpc_dispatcher）
├── example/                官方示例（随 mprpc 库一起由 CMake 构建）
│   ├── CMakeLists.txt
│   ├── generated/          user/friend 的 protoc 生成代码（pb.h/pb.cc）
│   ├── callee/             服务提供方（userservice / friendservice）→ provider
│   ├── caller/             服务调用方（calluserservice / callfriendservice）→ consumer
│   ├── async_channel/      协程异步 RPC 三进程链路（A/B 服务 + consumer）
│   ├── http_server/        纯 HTTP Servlet 示例
│   └── async_http/         HTTP 前端 + RPC 后端（/qps /block /nonblock）
├── tests/                  CMake 单测（http_codec / rpc_codec / process_signal）
├── scripts/                示例脚手架生成器
│   ├── coroutine_rpc_generator.py
│   ├── README.md
│   └── templates/          生成模板（common/rpc/http/async_http）
├── autobuild.sh            一键 cmake 构建脚本
├── CMakeLists.txt          顶层工程（构建 mprpc 库、example、可选 tests）
└── README.md
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

## 示例脚手架使用说明

仓库内置示例生成器 `scripts/coroutine_rpc_generator.py`，可一键生成**独立可构建**的 rpc / http / async_http 三类示例工程，业务代码完整复刻本仓库 `example/` 下的官方示例。

```text
scripts/
  coroutine_rpc_generator.py
  README.md
  templates/
    common/      顶层 CMake / conf / run.sh / shutdown.sh
    rpc/         provider + consumer（user / friend / generic 业务体）
    http/        http_server
    async_http/  backend + server（queryage / generic 业务体）
```

## 环境要求

| 组件                               | 说明                                                                          |
| ---------------------------------- | ----------------------------------------------------------------------------- |
| Python 3.6+                        | 生成器运行环境（仅标准库，跨平台，Windows 也可运行）                          |
| Linux / WSL                        | 生成工程**构建与运行**必须（框架协程汇编 `coctx_swap.S` 仅支持 Linux x86-64） |
| 本仓库                             | 生成工程 CMake 通过 `MPRPC_ROOT` 指向本仓库 `src/` 就地编译 mprpc             |
| `protoc` + protobuf / zookeeper 库 | 生成工程构建期需要（rpc / async_http 模式）                                   |

## 用法

```bash
python3 scripts/coroutine_rpc_generator.py <rpc|http|async_http> [选项]
```

公共选项：

| 选项               | 默认                  | 说明                                          |
| ------------------ | --------------------- | --------------------------------------------- |
| `-o, --output DIR` | `./`                  | 输出父目录，工程落在 `{DIR}/{name}/`          |
| `-n, --name NAME`  | proto 文件名 / `demo` | 工程目录名、可执行文件、conf 前缀             |
| `--demo MODE`      | `auto`                | 业务体：`user`/`friend`/`queryage`/`none`     |
| `--zk-port N`      | `2181`                | 写入 conf 的 ZooKeeper 端口                   |
| `--force`          | 关                    | 覆盖已存在的业务 `*.cc`、`conf/*`、`bin/*.sh` |

`MPRPC_ROOT`：生成器会自动取本仓库根目录写入生成的 CMake；仓库路径变化时可在构建期用 `-DMPRPC_ROOT=<仓库路径>` 覆盖。

生成目录说明：**每次生成自动创建工程内 `build/` 目录**，无需手动 `mkdir`。

### rpc（provider + consumer，单服务）

```bash
python3 scripts/coroutine_rpc_generator.py rpc -i proto/user.proto [-n name] [-o DIR]
# 可选 --provider-port N（默认 8000）
```

自动识别业务体：服务含 `Login+Register` → `user`；含 `GetFriendsList` → `friend`；其它 proto → `none`（通用可编译骨架，业务留 TODO）。也可 `--demo user|friend|none` 强制指定。

### http（纯 HTTP Server，无 proto / ZK）

```bash
python3 scripts/coroutine_rpc_generator.py http [-n name] [-o DIR] \
  [--servlet-path /hello] [--port 8080]
```

`--servlet-path` 可多次指定路由；`/echo` 使用回显语义，其余路径返回 `hello <name>`。

### async_http（RPC backend + HTTP 前端 server）

```bash
python3 scripts/coroutine_rpc_generator.py async_http -i proto/async_http_example.proto \
  [-n name] [-o DIR] [--server-port 8100] [--backend-port 8101]
```

自动识别业务体：方法含 `QueryAge` → `queryage`（复刻 `/qps /block /nonblock` 三 Servlet）；否则 → `none` 通用骨架（调用该服务第一个 rpc 方法）。

## 生成示例

```bash
mkdir -p ~/demos
python3 scripts/coroutine_rpc_generator.py rpc        -i proto/user.proto                  -n demo_rpc   -o ~/demos
python3 scripts/coroutine_rpc_generator.py http       -n demo_http --servlet-path /hello --servlet-path /echo --port 8080 -o ~/demos
python3 scripts/coroutine_rpc_generator.py async_http -i proto/async_http_example.proto    -n demo_async -o ~/demos
```

### 生成工程布局（以 `-n demo` 为例）

```text
demo/
├── build/                 # 生成器自动创建，供 cmake -B build 使用
├── CMakeLists.txt         # 独立工程：glob 编译仓库 src/ 为 mprpc + protoc 生成 pb
├── README.md              # 该工程的构建 / 运行说明
├── proto/demo.proto       # rpc / async_http：输入 proto 拷贝
├── src/                   # 见下表
├── conf/                  # conf/{name}.conf 或 conf/{name}_{server,backend}.conf
└── bin/                   # run.sh / shutdown.sh
```

| 类型       | src/                        | 可执行文件                               | conf                                |
| ---------- | --------------------------- | ---------------------------------------- | ----------------------------------- |
| rpc        | `provider.cc` `consumer.cc` | `provider` `consumer`                    | `conf/{name}.conf`                  |
| http       | `http_server.cc`            | `http_server`                            | `conf/{name}.conf`                  |
| async_http | `backend.cc` `server.cc`    | `async_http_backend` `async_http_server` | `conf/{name}_{backend,server}.conf` |

## 构建生成工程

```bash
cd ~/demos/demo_http
cmake -B build
cmake --build build -j"$(nproc)"
# 产物在 build/bin/
```

## 运行生成工程

每个工程两条启动路线（**二选一，勿重复**）：脚本 `sh bin/run.sh`，或手动执行二进制 + `-i conf`。

### rpc（需先启动 ZooKeeper）

```bash
# 手动
./build/bin/provider -i conf/demo_rpc.conf     # 终端1，阻塞
./build/bin/consumer -i conf/demo_rpc.conf     # 终端2，打印 Login/Register 结果
# 或脚本
sh bin/run.sh provider && sh bin/run.sh consumer
```

### http（纯 HTTP，无需 ZooKeeper）

```bash
./build/bin/http_server -i conf/demo_http.conf
curl -s 'http://127.0.0.1:8080/hello?name=world'    # → hello world
curl -s -d 'hello body' http://127.0.0.1:8080/echo   # → 原样回显
```

### async_http（需先启动 ZooKeeper）

```bash
# 手动
./build/bin/async_http_backend -i conf/demo_async_backend.conf   # RPC 后端 8101
./build/bin/async_http_server  -i conf/demo_async_server.conf    # HTTP 前端 8100
# 或脚本：sh bin/run.sh（按依赖顺序启动 backend → server）

curl -s 'http://127.0.0.1:8100/qps?id=1'       # 纯路由，不打 RPC，立即返回
curl -s 'http://127.0.0.1:8100/block?id=1'     # Servlet 内同步 RPC，约 1s（后端 sleep(1)）
curl -s 'http://127.0.0.1:8100/nonblock?id=1'  # RpcAsyncChannel 异步 RPC + wait，约 1s
```

`/nonblock` 异步生效的判定：前端业务日志依次出现
`starts async RPC → callback → resumed after async RPC`，且 RPC 网络收发发生在与 HTTP 分发**不同的 IO 线程**；后端业务日志出现 `QueryService.QueryAge finished, id=1, age=19`。

## run.sh 与 shutdown.sh

- `sh bin/run.sh`：按依赖顺序一键启动全部进程；`sh bin/run.sh <exe>` 只启动单个。
- `sh bin/shutdown.sh`：停止全部；`sh bin/shutdown.sh <exe>` 停止单个。
- 每次 `run.sh` 先自动停同名旧进程再启动，重复执行不会堆积多实例。
- 脚本按“可执行文件真实路径”（`/proc/<pid>/exe`）匹配进程，不误杀无关同名进程。
- 脚本自动按进程名匹配 conf（如 `async_http_server` → `conf/{name}_server.conf`）。
- 进程是一次性的（如 rpc 的 `consumer`）执行完即退出属正常。

## 日志位置

生成的 conf 中 `log_path` 为**绝对路径 `<工程>/build/bin/log`**，与从哪个目录启动无关：

- 框架日志按类型分文件落盘：RPC 日志（`DebugLog/InfoLog/...`）生成 `*.rpc_*.log`，业务日志（`AppInfoLog/...`）生成 `*.app_*.log`。**app 日志仅在代码调用过 `App*` 宏时才生成**——例如 demo*http 只用 `InfoLog/ErrorLog`，因此只有 `\*\_rpc*\*.log`，属正常。
- `*.nohup_log` 是 run.sh 用 nohup 重定向的进程 stdout/stderr 转储；框架日志不写 stdout，**多数情况下为空属正常**（std::cout / std::cerr 输出除外）。
- 排查启动失败看 `build/bin/log/<进程名>.nohup_log`（cerr 错误会进去）与框架日志文件。

## 幂等与覆盖

| 文件                                           | 行为                                       |
| ---------------------------------------------- | ------------------------------------------ |
| `src/*.cc`、`conf/*`、`bin/*.sh`               | 已存在则跳过（业务可编辑），`--force` 覆盖 |
| `build/`（缺失时）                             | 每次生成自动补齐                           |
| `CMakeLists.txt`、`README.md`、`proto/*.proto` | 每次重新生成                               |

修改 proto 后需加 `--force` 重新生成，保证 provider 的方法集与 pb 一致。

## 实现要点与 FAQ

- 模板引擎：`string.Template`（`${TOKEN}`）；proto 仅轻量正则解析 package/service/rpc 方法，pb 由生成工程构建期调 `protoc` 产出。
- 独立 CMake 不复用 `add_subdirectory(本仓库/src)`（`src/CMakeLists.txt` 依赖 `PROJECT_SOURCE_DIR/src`），而是 glob 复刻其语义并指向 `MPRPC_ROOT`。
- `protoc: command not found`：生成工程构建期需要，安装 protobuf 编译器并加入 PATH。
- 配置 CMake 提示 `MPRPC_ROOT` 无效：传 `-DMPRPC_ROOT=<本仓库绝对路径>`。
- 任意 proto 生成空骨架而非示例业务：方法名未命中 `Login/Register`、`GetFriendsList`、`QueryAge`，属预期；可用 `--demo user|friend|queryage` 强制套用示例 schema。
- Windows 上可生成但不能编译：框架协程汇编仅 Linux；把生成工程拷到 Linux/WSL 构建。
