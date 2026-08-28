# coroutine_rpc

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
