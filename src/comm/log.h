#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "comm/config.h"

namespace crpc {

enum class LogType {
  RPC_LOG = 1,
  APP_LOG = 2,
};

class AsyncLogger {
 public:
  using ptr = std::shared_ptr<AsyncLogger>;

  AsyncLogger(std::string file_name, std::string file_path,
              std::size_t max_size, LogType log_type);
  ~AsyncLogger();

  AsyncLogger(const AsyncLogger&) = delete;
  AsyncLogger& operator=(const AsyncLogger&) = delete;

  void push(std::vector<std::string>& buffer);
  void flush();
  void stop();

 private:
  void execute();
  bool openLogFile(const std::string& date);
  bool rotateIfNeeded(std::size_t incoming_size);
  std::string makeFileName() const;
  void closeFile();

 private:
  std::string m_file_name;
  std::string m_file_path;
  std::size_t m_max_size {0};
  LogType m_log_type {LogType::RPC_LOG};
  int m_file_no {0};
  FILE* m_file_handle {nullptr};
  std::size_t m_file_size {0};
  std::string m_date;

  std::mutex m_mutex;
  std::condition_variable m_condition;
  std::condition_variable m_drained_condition;
  std::queue<std::vector<std::string>> m_tasks;
  bool m_stopping {false};
  bool m_writing {false};
  std::thread m_thread;
};

class Logger {
 public:
  static Logger& GetInstance();
  static Logger* GetLogger();

  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  void init(const Config& config);
  void pushRpcLog(const std::string& log_msg);
  void pushAppLog(const std::string& log_msg);
  void flush();
  void shutdown();

  bool isInitialized() const;
  bool shouldLog(LogLevel level, LogType type) const;

 private:
  Logger() = default;
  ~Logger();

  void syncLoop();
  void dispatchBuffers();

 private:
  mutable std::mutex m_state_mutex;
  std::mutex m_rpc_buffer_mutex;
  std::mutex m_app_buffer_mutex;
  std::mutex m_sync_mutex;
  std::condition_variable m_sync_condition;
  std::vector<std::string> m_rpc_buffer;
  std::vector<std::string> m_app_buffer;
  AsyncLogger::ptr m_async_rpc_logger;
  AsyncLogger::ptr m_async_app_logger;
  std::thread m_sync_thread;
  std::atomic<bool> m_initialized {false};
  std::atomic<bool> m_stopping {false};
  int m_sync_interval {500};
  LogLevel m_rpc_level {LogLevel::DEBUG};
  LogLevel m_app_level {LogLevel::DEBUG};
};

class LogEvent {
 public:
  LogEvent(LogLevel level, const char* file_name, int line,
           const char* func_name, LogType type);
  ~LogEvent();

  std::stringstream& getStringStream();

 private:
  void appendPrefix();

 private:
  LogLevel m_level;
  const char* m_file_name;
  int m_line {0};
  const char* m_func_name;
  LogType m_type;
  bool m_enabled {false};
  bool m_prefix_appended {false};
  std::stringstream m_stream;
};

class LogVoidify {
 public:
  void operator&(std::ostream&) {}
};

void InitLogger();
void FlushLogger();
void ShutdownLogger();
bool OpenLog();
bool ShouldLog(LogLevel level, LogType type);

inline std::string FormatString(const char* text) {
  return text == nullptr ? std::string() : std::string(text);
}

template <typename Arg, typename... Args>
std::string FormatString(const char* format, Arg&& arg, Args&&... args) {
  if (format == nullptr) {
    return {};
  }

  int size = std::snprintf(nullptr, 0, format, std::forward<Arg>(arg),
                           std::forward<Args>(args)...);
  if (size <= 0) {
    return {};
  }

  std::vector<char> buffer(static_cast<std::size_t>(size) + 1);
  std::snprintf(buffer.data(), buffer.size(), format, std::forward<Arg>(arg),
                std::forward<Args>(args)...);
  return std::string(buffer.data(), static_cast<std::size_t>(size));
}

template <typename... Args>
void AppLog(LogLevel level, const char* file_name, int line,
            const char* func_name, const char* format, Args&&... args) {
  if (!ShouldLog(level, LogType::APP_LOG)) {
    return;
  }
  LogEvent event(level, file_name, line, func_name, LogType::APP_LOG);
  event.getStringStream() << FormatString(format, std::forward<Args>(args)...);
}

}  // namespace crpc

#define CRPC_LOG_STREAM(level) \
  !crpc::ShouldLog(level, crpc::LogType::RPC_LOG) ? (void)0 : \
      crpc::LogVoidify() & \
          crpc::LogEvent(level, __FILE__, __LINE__, __func__, \
                         crpc::LogType::RPC_LOG).getStringStream()

#define DebugLog CRPC_LOG_STREAM(crpc::LogLevel::DEBUG)
#define InfoLog CRPC_LOG_STREAM(crpc::LogLevel::INFO)
#define WarnLog CRPC_LOG_STREAM(crpc::LogLevel::WARN)
#define ErrorLog CRPC_LOG_STREAM(crpc::LogLevel::ERROR)

#define CRPC_APP_LOG(level, format, ...) \
  do { \
    if (crpc::ShouldLog(level, crpc::LogType::APP_LOG)) { \
      crpc::AppLog(level, __FILE__, __LINE__, __func__, format, \
                   ##__VA_ARGS__); \
    } \
  } while (0)

#define AppDebugLog(format, ...) \
  CRPC_APP_LOG(crpc::LogLevel::DEBUG, format, ##__VA_ARGS__)
#define AppInfoLog(format, ...) \
  CRPC_APP_LOG(crpc::LogLevel::INFO, format, ##__VA_ARGS__)
#define AppWarnLog(format, ...) \
  CRPC_APP_LOG(crpc::LogLevel::WARN, format, ##__VA_ARGS__)
#define AppErrorLog(format, ...) \
  CRPC_APP_LOG(crpc::LogLevel::ERROR, format, ##__VA_ARGS__)
