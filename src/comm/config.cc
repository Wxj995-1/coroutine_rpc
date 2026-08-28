#include "comm/config.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <unordered_set>
#include <utility>

namespace crpc {
namespace {

using ConfigValues = std::unordered_map<std::string, std::string>;

void Trim(std::string& value) {
  const std::string whitespace = " \t\r\n";
  const std::size_t begin = value.find_first_not_of(whitespace);
  if (begin == std::string::npos) {
    value.clear();
    return;
  }
  const std::size_t end = value.find_last_not_of(whitespace);
  value = value.substr(begin, end - begin + 1);
}

std::string ToUpper(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::toupper(c));
                 });
  return value;
}

bool ParseLogLevel(const std::string& value, LogLevel& level) {
  const std::string normalized = ToUpper(value);
  if (normalized == "DEBUG") {
    level = LogLevel::DEBUG;
  } else if (normalized == "INFO") {
    level = LogLevel::INFO;
  } else if (normalized == "WARN") {
    level = LogLevel::WARN;
  } else if (normalized == "ERROR") {
    level = LogLevel::ERROR;
  } else if (normalized == "NONE") {
    level = LogLevel::NONE;
  } else {
    return false;
  }
  return true;
}

bool ReadInteger(const ConfigValues& values, const std::string& key,
                 long long minimum, long long maximum, long long& result) {
  const auto it = values.find(key);
  if (it == values.end()) {
    return true;
  }

  errno = 0;
  char* end = nullptr;
  const long long parsed = std::strtoll(it->second.c_str(), &end, 10);
  if (errno == ERANGE || end == it->second.c_str() || end == nullptr ||
      *end != '\0' || parsed < minimum || parsed > maximum) {
    std::cerr << "invalid config value: " << key << '=' << it->second
              << ", expected range [" << minimum << ", " << maximum << ']'
              << std::endl;
    return false;
  }

  result = parsed;
  return true;
}

bool ReadString(const ConfigValues& values, const std::string& key,
                std::string& result) {
  const auto it = values.find(key);
  if (it == values.end()) {
    return true;
  }
  if (it->second.empty()) {
    std::cerr << "invalid empty config value: " << key << std::endl;
    return false;
  }
  result = it->second;
  return true;
}

const std::unordered_set<std::string>& KnownKeys() {
  static const std::unordered_set<std::string> keys = {
      "rpcserverip",          "rpcserverport",
      "zookeeperip",         "zookeeperport",
      "log_path",            "log_prefix",
      "log_max_file_size",   "rpc_log_level",
      "app_log_level",       "log_sync_interval",
      "log_sync_inteval",    "cor_stack_size",
      "cor_pool_size",       "msg_req_len",
      "max_connect_timeout", "iothread_num",
      "timewheel_bucket_num", "timewheel_inteval"};
  return keys;
}

}  // namespace

LogLevel StringToLogLevel(const std::string& value) {
  LogLevel level = LogLevel::DEBUG;
  ParseLogLevel(value, level);
  return level;
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

Config& Config::Instance() {
  static Config config;
  return config;
}

Config* GetConfig() {
  return &Config::Instance();
}

bool Config::loadFromFile(const std::string& file_path) {
  if (m_loaded) {
    std::cerr << "config has already been loaded" << std::endl;
    return false;
  }

  std::ifstream file(file_path);
  if (!file.is_open()) {
    std::cerr << "config file does not exist: " << file_path << std::endl;
    return false;
  }

  ConfigValues values;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(file, line)) {
    ++line_number;
    Trim(line);
    if (line.empty() || line.front() == '#') {
      continue;
    }

    const std::size_t separator = line.find('=');
    if (separator == std::string::npos) {
      std::cerr << "ignore invalid config line " << line_number << ": "
                << line << std::endl;
      continue;
    }

    std::string key = line.substr(0, separator);
    std::string value = line.substr(separator + 1);
    Trim(key);
    Trim(value);
    if (key.empty()) {
      std::cerr << "ignore config line with empty key: " << line_number
                << std::endl;
      continue;
    }
    values[key] = value;
  }

  std::string rpc_server_ip = m_rpc_server_ip;
  std::string zookeeper_ip = m_zookeeper_ip;
  std::string log_path = m_log_path;
  std::string log_prefix = m_log_prefix;
  long long rpc_server_port = m_rpc_server_port;
  long long zookeeper_port = m_zookeeper_port;
  long long log_max_file_size_mb =
      static_cast<long long>(m_log_max_file_size / 1024 / 1024);
  long long log_sync_interval = m_log_sync_interval;
  long long cor_stack_size = m_cor_stack_size;
  long long cor_pool_size = m_cor_pool_size;
  long long msg_req_len = m_msg_req_len;
  long long max_connect_timeout = m_max_connect_timeout;
  long long iothread_num = m_iothread_num;
  long long timewheel_bucket_num = m_timewheel_bucket_num;
  long long timewheel_interval = m_timewheel_inteval;
  LogLevel rpc_log_level = m_log_level;
  LogLevel app_log_level = m_app_log_level;

  if (!ReadString(values, "rpcserverip", rpc_server_ip) ||
      !ReadString(values, "zookeeperip", zookeeper_ip) ||
      !ReadString(values, "log_path", log_path) ||
      !ReadString(values, "log_prefix", log_prefix) ||
      !ReadInteger(values, "rpcserverport", 1, 65535, rpc_server_port) ||
      !ReadInteger(values, "zookeeperport", 1, 65535, zookeeper_port) ||
      !ReadInteger(values, "log_max_file_size", 1,
                   INT_MAX / (1024 * 1024), log_max_file_size_mb) ||
      !ReadInteger(values, "cor_stack_size", 1, INT_MAX, cor_stack_size) ||
      !ReadInteger(values, "cor_pool_size", 1, INT_MAX, cor_pool_size) ||
      !ReadInteger(values, "msg_req_len", 1, INT_MAX, msg_req_len) ||
      !ReadInteger(values, "max_connect_timeout", 1, INT_MAX,
                   max_connect_timeout) ||
      !ReadInteger(values, "iothread_num", 1, INT_MAX, iothread_num) ||
      !ReadInteger(values, "timewheel_bucket_num", 1, INT_MAX,
                   timewheel_bucket_num) ||
      !ReadInteger(values, "timewheel_inteval", 1, INT_MAX,
                   timewheel_interval)) {
    return false;
  }

  const std::string sync_key = values.count("log_sync_interval") != 0
                                   ? "log_sync_interval"
                                   : "log_sync_inteval";
  if (!ReadInteger(values, sync_key, 1, INT_MAX, log_sync_interval)) {
    return false;
  }

  const auto rpc_level_it = values.find("rpc_log_level");
  if (rpc_level_it != values.end() &&
      !ParseLogLevel(rpc_level_it->second, rpc_log_level)) {
    std::cerr << "invalid config value: rpc_log_level="
              << rpc_level_it->second << std::endl;
    return false;
  }
  const auto app_level_it = values.find("app_log_level");
  if (app_level_it != values.end() &&
      !ParseLogLevel(app_level_it->second, app_log_level)) {
    std::cerr << "invalid config value: app_log_level="
              << app_level_it->second << std::endl;
    return false;
  }

  m_rpc_server_ip = std::move(rpc_server_ip);
  m_rpc_server_port = static_cast<uint16_t>(rpc_server_port);
  m_zookeeper_ip = std::move(zookeeper_ip);
  m_zookeeper_port = static_cast<uint16_t>(zookeeper_port);
  m_log_path = std::move(log_path);
  m_log_prefix = std::move(log_prefix);
  m_log_max_file_size =
      static_cast<std::size_t>(log_max_file_size_mb) * 1024 * 1024;
  m_log_level = rpc_log_level;
  m_app_log_level = app_log_level;
  m_log_sync_interval = static_cast<int>(log_sync_interval);
  m_cor_stack_size = static_cast<int>(cor_stack_size);
  m_cor_pool_size = static_cast<int>(cor_pool_size);
  m_msg_req_len = static_cast<int>(msg_req_len);
  m_max_connect_timeout = static_cast<int>(max_connect_timeout);
  m_iothread_num = static_cast<int>(iothread_num);
  m_timewheel_bucket_num = static_cast<int>(timewheel_bucket_num);
  m_timewheel_inteval = static_cast<int>(timewheel_interval);

  for (const auto& item : values) {
    if (KnownKeys().count(item.first) == 0) {
      m_extra_values[item.first] = item.second;
    }
  }
  m_loaded = true;
  return true;
}

std::string Config::get(const std::string& key) const {
  if (key == "rpcserverip") return m_rpc_server_ip;
  if (key == "rpcserverport") return std::to_string(m_rpc_server_port);
  if (key == "zookeeperip") return m_zookeeper_ip;
  if (key == "zookeeperport") return std::to_string(m_zookeeper_port);
  if (key == "log_path") return m_log_path;
  if (key == "log_prefix") return m_log_prefix;
  if (key == "log_max_file_size") {
    return std::to_string(m_log_max_file_size / 1024 / 1024);
  }
  if (key == "rpc_log_level") return LogLevelToString(m_log_level);
  if (key == "app_log_level") return LogLevelToString(m_app_log_level);
  if (key == "log_sync_interval" || key == "log_sync_inteval") {
    return std::to_string(m_log_sync_interval);
  }
  if (key == "cor_stack_size") return std::to_string(m_cor_stack_size);
  if (key == "cor_pool_size") return std::to_string(m_cor_pool_size);
  if (key == "msg_req_len") return std::to_string(m_msg_req_len);
  if (key == "max_connect_timeout") {
    return std::to_string(m_max_connect_timeout);
  }
  if (key == "iothread_num") return std::to_string(m_iothread_num);
  if (key == "timewheel_bucket_num") {
    return std::to_string(m_timewheel_bucket_num);
  }
  if (key == "timewheel_inteval") {
    return std::to_string(m_timewheel_inteval);
  }

  const auto it = m_extra_values.find(key);
  return it == m_extra_values.end() ? std::string() : it->second;
}

}  // namespace crpc
