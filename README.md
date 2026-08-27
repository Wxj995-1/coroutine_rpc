# coroutine_rpc

## 日志配置

日志系统在 `MprpcApplication::Init` 完成配置加载后自动启动。框架内部使用
`DebugLog`、`InfoLog`、`WarnLog`、`ErrorLog`，业务代码使用
`AppDebugLog`、`AppInfoLog`、`AppWarnLog`、`AppErrorLog`。

配置文件支持：

```ini
log_path=./log/
log_prefix=coroutine_rpc
log_max_file_size=5
rpc_log_level=DEBUG
app_log_level=DEBUG
log_sync_interval=500
```

`log_max_file_size` 的单位是 MB，刷新间隔的单位是毫秒。日志会分别写入
`coroutine_rpc_YYYYMMDD_rpc_N.log` 和
`coroutine_rpc_YYYYMMDD_app_N.log`，并按日期和文件大小自动切割。
