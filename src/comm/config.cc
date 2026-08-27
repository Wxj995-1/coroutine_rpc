#include "comm/config.h"
#include "mprpcapplication.h"
#include <algorithm>
#include <cctype>
#include <limits>
#include <stdlib.h>

namespace crpc {

static Config* g_config = nullptr;

LogLevel StringToLogLevel(const std::string& value) {
  std::string level = value;
  std::transform(level.begin(), level.end(), level.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });

  if (level == "INFO") return LogLevel::INFO;
  if (level == "WARN") return LogLevel::WARN;
  if (level == "ERROR") return LogLevel::ERROR;
  if (level == "NONE") return LogLevel::NONE;
  return LogLevel::DEBUG;
}

const char* LogLevelToString(LogLevel level) {
  switch (level) {
    case LogLevel::DEBUG: return "DEBUG";
    case LogLevel::INFO: return "INFO";
    case LogLevel::WARN: return "WARN";
    case LogLevel::ERROR: return "ERROR";
    case LogLevel::NONE: return "NONE";
  }
  return "DEBUG";
}

Config* GetConfig() {
  if (!g_config) {
    g_config = new Config();
    g_config->initFromFile();
  }
  return g_config;
}

void Config::initFromFile() {
  MprpcConfig& cfg = MprpcApplication::GetConfig();
  auto load_int = [&cfg](const std::string& key, int def) {
    std::string v = cfg.Load(key);
    if (v.empty()) {
      return def;
    }
    return atoi(v.c_str());
  };
  auto load_string = [&cfg](const std::string& key, const std::string& def) {
    std::string value = cfg.Load(key);
    return value.empty() ? def : value;
  };

  m_log_path = load_string("log_path", m_log_path);
  m_log_prefix = load_string("log_prefix", m_log_prefix);
  int max_file_size_mb = load_int("log_max_file_size", 5);
  if (max_file_size_mb > 0 &&
      max_file_size_mb <= std::numeric_limits<int>::max() / (1024 * 1024)) {
    m_log_max_file_size = static_cast<std::size_t>(max_file_size_mb) * 1024 * 1024;
  }
  m_log_level = StringToLogLevel(load_string("rpc_log_level", "DEBUG"));
  m_app_log_level = StringToLogLevel(load_string("app_log_level", "DEBUG"));
  std::string sync_interval = cfg.Load("log_sync_interval");
  if (sync_interval.empty()) {
    sync_interval = cfg.Load("log_sync_inteval");  // TinyRPC 原配置的拼写
  }
  if (!sync_interval.empty()) {
    m_log_sync_interval = atoi(sync_interval.c_str());
  }
  if (m_log_sync_interval <= 0) {
    m_log_sync_interval = 500;
  }

  m_cor_stack_size = load_int("cor_stack_size", m_cor_stack_size);
  m_cor_pool_size = load_int("cor_pool_size", m_cor_pool_size);
  m_msg_req_len = load_int("msg_req_len", m_msg_req_len);
  m_max_connect_timeout = load_int("max_connect_timeout", m_max_connect_timeout);
  m_iothread_num = load_int("iothread_num", m_iothread_num);
  m_timewheel_bucket_num = load_int("timewheel_bucket_num", m_timewheel_bucket_num);
  m_timewheel_inteval = load_int("timewheel_inteval", m_timewheel_inteval);
}

}  // namespace crpc
