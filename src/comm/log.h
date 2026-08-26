#pragma once
#include <sstream>
#include <string>
#include <stdio.h>
#include <string.h>
#include "logger.h"

namespace crpc {

enum class LogLevel {
  DEBUG = 1,
  INFO = 2,
  WARN = 3,
  ERROR = 4,
};

class LogEvent {
 public:
  LogEvent(LogLevel level, const char* file, int line, const char* func)
      : m_level(level), m_file(file), m_line(line), m_func(func) {}

  ~LogEvent() {
    Logger& logger = Logger::GetInstance();
    logger.SetLogLevel(m_level == LogLevel::ERROR ? ::ERROR : ::INFO);
    char prefix[256] = {0};
    snprintf(prefix, sizeof(prefix), "[%s:%d %s] ", m_file, m_line, m_func);
    logger.Log(std::string(prefix) + m_ss.str());
  }

  template <class T>
  LogEvent& operator<<(const T& v) {
    m_ss << v;
    return *this;
  }

 private:
  LogLevel m_level;
  const char* m_file;
  int m_line;
  const char* m_func;
  std::ostringstream m_ss;
};

}  // namespace crpc

#define DebugLog crpc::LogEvent(crpc::LogLevel::DEBUG, LOG_BASENAME(__FILE__), __LINE__, __FUNCTION__)
#define InfoLog  crpc::LogEvent(crpc::LogLevel::INFO,  LOG_BASENAME(__FILE__), __LINE__, __FUNCTION__)
#define WarnLog  crpc::LogEvent(crpc::LogLevel::WARN,  LOG_BASENAME(__FILE__), __LINE__, __FUNCTION__)
#define ErrorLog crpc::LogEvent(crpc::LogLevel::ERROR, LOG_BASENAME(__FILE__), __LINE__, __FUNCTION__)
