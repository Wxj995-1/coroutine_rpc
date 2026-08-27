#pragma once
#include <cstddef>
#include <string>

namespace crpc {

enum class LogLevel {
  DEBUG = 1,
  INFO = 2,
  WARN = 3,
  ERROR = 4,
  NONE = 5,
};

LogLevel StringToLogLevel(const std::string& value);
const char* LogLevelToString(LogLevel level);

class Config {
 public:
  std::string m_log_path {"./log/"};
  std::string m_log_prefix {"coroutine_rpc"};
  std::size_t m_log_max_file_size {5 * 1024 * 1024};
  LogLevel m_log_level {LogLevel::DEBUG};
  LogLevel m_app_log_level {LogLevel::DEBUG};
  int m_log_sync_interval {500};  // ms

  int m_cor_stack_size {128 * 1024};
  int m_cor_pool_size {1024};
  int m_msg_req_len {20};
  int m_max_connect_timeout {10000};  // ms
  int m_iothread_num {4};
  int m_timewheel_bucket_num {10};
  int m_timewheel_inteval {10};  // second

  void initFromFile();
};

Config* GetConfig();

}  // namespace crpc
