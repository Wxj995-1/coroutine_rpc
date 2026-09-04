# coroutine_rpc 示例脚手架

设计框架：一个 python3 脚本 + `templates/` 目录，
根据参数生成**独立可构建**的示例工程，完整复刻仓库 `example/` 下的三类官方示例。

## 目录

```
scripts/
  coroutine_rpc_generator.py
  README.md
  templates/
    common/          顶层 CMake / conf / run.sh / shutdown.sh 模板
    rpc/             provider + consumer（user / friend / generic 业务体）
    http/            http_server
    async_http/      backend + server（queryage / generic 业务体）
```

## 环境要求

| 组件                             | 说明                                                                          |
| -------------------------------- | ----------------------------------------------------------------------------- |
| Python 3.6+                      | 生成器运行环境（仅标准库，跨平台，Windows 也可运行）                          |
| Linux / WSL                      | 生成工程**构建与运行**必须（框架协程汇编 `coctx_swap.S` 仅支持 Linux x86-64） |
| coroutine_rpc 仓库               | 生成工程 CMake 通过 `MPRPC_ROOT` 指向其 `src/` 就地编译 mprpc                 |
| `protoc` + protobuf/zookeeper 库 | 生成工程构建期需要（rpc / async_http 模式）                                   |

## 用法

```
python3 scripts/coroutine_rpc_generator.py <rpc|http|async_http> [选项]
```

### 公共选项

| 选项               | 默认                  | 说明                                             |
| ------------------ | --------------------- | ------------------------------------------------ |
| `-o, --output DIR` | `./`                  | 输出父目录，工程落在 `{DIR}/{name}/`             |
| `-n, --name NAME`  | proto 文件名 / `demo` | 工程目录名、可执行文件、conf 前缀                |
| `--demo MODE`      | `auto`                | 业务体：`user`/`friend`/`queryage`/`none`/`auto` |
| `--zk-port N`      | `2181`                | 写入 conf 的 ZooKeeper 端口                      |
| `--force`          | 关                    | 覆盖已存在的业务 `*.cc`、`conf/*`、`bin/*.sh`    |

`MPRPC_ROOT`：默认取“生成器所在仓库根目录”写入生成 CMake，也可在构建时用
`-DMPRPC_ROOT=<仓库路径>` 覆盖。

每次生成会在工程内自动创建 `build/` 目录，无需手动 `mkdir`。

更完整的构建 / 运行 / run.sh 测试步骤见仓库根目录 `README.md` 的
“示例脚手架使用说明”章节。

### rpc —— 1 个 provider + 1 个 consumer（单服务）

```
python3 scripts/coroutine_rpc_generator.py rpc -i proto/user.proto [-n name] [-o DIR]
选项：
  --provider-port N   provider 监听端口，默认 8000
```

自动识别业务体：服务含 `Login+Register` → `user`；含 `GetFriendsList` → `friend`；
其它 proto → `none` 通用可编译骨架。

### http —— 1 个纯 HTTP server（Servlet 路由，无 proto / ZK）

```
python3 scripts/coroutine_rpc_generator.py http [-n name] [-o DIR] \
  [--servlet-path /hello] [--port 8080]
```

`--servlet-path` 可多次指定路由；`/echo` 使用回显语义，其余路径返回 `hello <name>`。

### async_http —— 1 个 RPC backend + 1 个 HTTP 前端 server

```
python3 scripts/coroutine_rpc_generator.py async_http -i proto/async_http_example.proto \
  [-n name] [-o DIR] [--server-port 8100] [--backend-port 8101]
```

自动识别业务体：方法含 `QueryAge` → `queryage`（完整复刻示例）；否则 → `none` 通用骨架
（调用该服务第一个 rpc 方法）。

## 生成工程布局（以 `-n demo` 为例）

```
demo/
├── CMakeLists.txt            # 独立工程：glob 编译仓库 src/ 为 mprpc + protoc 生成 pb
├── README.md                 # 该工程的构建 / 运行说明
├── proto/demo.proto          # rpc / async_http：输入 proto 拷贝
├── src/                      # 见下表
├── conf/
│   ├── demo.conf             # rpc/http 共用 或 demo_server.conf + demo_backend.conf
└── bin/
    ├── run.sh                # 启动（一键 or 按进程名）
    └── shutdown.sh           # 停止
```

| 类型       | src/                        | 可执行文件                               | conf                                |
| ---------- | --------------------------- | ---------------------------------------- | ----------------------------------- |
| rpc        | `provider.cc` `consumer.cc` | `provider` `consumer`                    | `conf/{name}.conf`                  |
| http       | `http_server.cc`            | `http_server`                            | `conf/{name}.conf`                  |
| async_http | `backend.cc` `server.cc`    | `async_http_backend` `async_http_server` | `conf/{name}_{backend,server}.conf` |

## 构建生成工程

```bash
cd <name>
cmake -B build                # 或：cmake -B build -DMPRPC_ROOT=<coroutine_rpc 仓库>
cmake --build build -j"$(nproc)"
# 产物在 build/bin/
```

## 运行

### rpc（需先启动 ZooKeeper）

```bash
# 手动方式
./build/bin/provider -i conf/<name>.conf
./build/bin/consumer -i conf/<name>.conf
# 或脚本方式（二选一，勿重复）
sh bin/run.sh provider && sh bin/run.sh consumer
sh bin/shutdown.sh
```

### http（纯 HTTP，无需 ZooKeeper）

```bash
./build/bin/http_server -i conf/<name>.conf        # 手动
curl 'http://127.0.0.1:8080/hello?name=world'
curl -d 'hello' http://127.0.0.1:8080/echo
```

### async_http（需先启动 ZooKeeper）

```bash
# 手动方式
./build/bin/async_http_backend -i conf/<name>_backend.conf   # RPC 后端
./build/bin/async_http_server  -i conf/<name>_server.conf    # HTTP 前端
# 或脚本方式
sh bin/run.sh          # 按依赖顺序启动 backend -> server
sh bin/shutdown.sh

curl 'http://127.0.0.1:8100/qps?id=1'       # 纯路由，不打 RPC
curl 'http://127.0.0.1:8100/block?id=1'     # Servlet 内同步 RPC
curl 'http://127.0.0.1:8100/nonblock?id=1'  # Servlet 内 RpcAsyncChannel 异步 RPC + wait
```

前端日志可见 `starts async RPC -> 回调 -> resumed after async RPC`；后端可见
`QueryService.QueryAge finished`。

## run.sh / shutdown.sh

位于生成工程 `bin/`。脚本按“可执行文件真实路径”匹配进程，不会误杀同名无关进程。

```bash
sh bin/run.sh                  # 启动该工程全部进程（按依赖顺序）
sh bin/run.sh provider         # 只启动单个进程
sh bin/shutdown.sh             # 停止全部
sh bin/shutdown.sh provider    # 停止单个
```

脚本自动为进程匹配 conf（如 `async_http_server` → `conf/{name}_server.conf`），日志追加到
`build/bin/log/{进程名}.nohup_log`。

生成的 conf 中 `log_path` 为**绝对路径** `<工程>/build/bin/log`，框架日志与 nohup 日志都落在该目录，
与从哪个目录启动无关。

## 幂等与覆盖

| 文件                                           | 行为                                       |
| ---------------------------------------------- | ------------------------------------------ |
| `src/*.cc`、`conf/*`、`bin/*.sh`               | 已存在则跳过（业务可编辑），`--force` 覆盖 |
| `CMakeLists.txt`、`README.md`、`proto/*.proto` | 每次重新生成                               |

修改 proto 后需加 `--force` 重新生成，保证 provider 的方法集与 pb 一致。

## 生成器实现要点

- 模板引擎：`string.Template`（`${TOKEN}`），风格同 tinyrpc。
- proto 只做轻量正则解析（package / service / rpc 方法三元组），不依赖 protoc；
  生成工程在构建期由 CMake 调 `protoc` 产出 pb 代码。
- 独立 CMake 不复用 `add_subdirectory(<repo>/src)`（`src/CMakeLists.txt` 依赖
  `PROJECT_SOURCE_DIR/src`），而是 glob 复刻其语义并指向 `MPRPC_ROOT`。
- 业务模板 = 仓库 example 本体；`--demo none` 得到任意 proto 都能编译的通用骨架。

## 常见问题

- `protoc: command not found`：生成工程构建期需要，安装 protobuf 编译器并加入 PATH。
- 配置 CMake 时提示 `MPRPC_ROOT` 无效：传 `-DMPRPC_ROOT=<coroutine_rpc 仓库绝对路径>`。
- 任意 proto 生成空骨架而非示例业务：方法名未命中 `Login/Register`、`GetFriendsList`、
  `QueryAge`，属预期；可用 `--demo user|friend|queryage` 强制套用示例 schema。
- Windows 上可生成但不能编译：框架协程汇编仅 Linux；把生成工程拷到 Linux/WSL 构建。
