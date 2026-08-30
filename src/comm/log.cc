#include "comm/log.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "comm/run_time.h"
#include "coroutine/coroutine.h"

namespace crpc {
namespace {

const char* LogTypeToString(LogType type) {
  return type == LogType::APP_LOG ? "app" : "rpc";
}

pid_t CurrentThreadId() {
  static thread_local pid_t thread_id = 0;
  if (thread_id == 0) {
    thread_id = static_cast<pid_t>(::syscall(SYS_gettid));
  }
  return thread_id;
}

std::string CurrentDate() {
  timeval now {};
  gettimeofday(&now, nullptr);
  tm local_time {};
  localtime_r(&now.tv_sec, &local_time);
  char date[16] = {0};
  strftime(date, sizeof(date), "%Y%m%d", &local_time);
  return date;
}

const char* BaseName(const char* path) {
  if (path == nullptr) {
    return "";
  }
  const char* slash = std::strrchr(path, '/');
  return slash == nullptr ? path : slash + 1;
}

std::string NormalizeLogPath(std::string path) {
  if (path.empty()) {
    path = "./";
  }
  if (path.back() != '/') {
    path.push_back('/');
  }
  return path;
}

bool CreateDirectory(const std::string& raw_path) {
  std::string path = NormalizeLogPath(raw_path);
  while (path.size() > 1 && path.back() == '/') {
    path.pop_back();
  }
  if (path.empty() || path == "." || path == "/") {
    return true;
  }

  for (std::size_t i = 1; i <= path.size(); ++i) {
    if (i != path.size() && path[i] != '/') {
      continue;
    }
    std::string part = path.substr(0, i);
    if (part.empty() || part == ".") {
      continue;
    }
    if (::mkdir(part.c_str(), 0755) != 0 && errno != EEXIST) {
      std::cerr << "create log directory failed: " << part
                << ", error=" << std::strerror(errno) << std::endl;
      return false;
    }
  }
  return true;
}

}  // namespace

AsyncLogger::AsyncLogger(std::string file_name, std::string file_path,
                         std::size_t max_size, LogType log_type)
    : m_file_name(std::move(file_name)),
      m_file_path(NormalizeLogPath(std::move(file_path))),
      m_max_size(max_size),
      m_log_type(log_type) {
  CreateDirectory(m_file_path);
  m_thread = std::thread(&AsyncLogger::execute, this);
}

AsyncLogger::~AsyncLogger() {
  stop();
}

void AsyncLogger::push(std::vector<std::string>& buffer) {
  if (buffer.empty()) {
    return;
  }

  std::vector<std::string> task;
  task.swap(buffer);
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_stopping) {
      return;
    }
    m_tasks.push(std::move(task));
  }
  m_condition.notify_one();
}

void AsyncLogger::flush() {
  std::unique_lock<std::mutex> lock(m_mutex);
  m_drained_condition.wait(lock, [this]() {
    return m_tasks.empty() && !m_writing;
  });
  if (m_file_handle != nullptr) {
    std::fflush(m_file_handle);
  }
}

void AsyncLogger::stop() {
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_stopping && !m_thread.joinable()) {
      return;
    }
    m_stopping = true;
  }
  m_condition.notify_all();
  if (m_thread.joinable()) {
    m_thread.join();
  }
  closeFile();
}

void AsyncLogger::execute() {
  for (;;) {
    std::vector<std::string> task;
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_condition.wait(lock, [this]() {
        return m_stopping || !m_tasks.empty();
      });
      if (m_tasks.empty()) {
        if (m_stopping) {
          break;
        }
        continue;
      }
      task = std::move(m_tasks.front());
      m_tasks.pop();
      m_writing = true;
    }

    for (const std::string& message : task) {
      if (message.empty()) {
        continue;
      }
      const std::string date = CurrentDate();
      if (!openLogFile(date) || !rotateIfNeeded(message.size())) {
        continue;
      }
      const std::size_t written =
          std::fwrite(message.data(), 1, message.size(), m_file_handle);
      m_file_size += written;
      if (written != message.size()) {
        std::cerr << "write log file failed: " << makeFileName()
                  << ", error=" << std::strerror(errno) << std::endl;
      }
    }
    if (m_file_handle != nullptr) {
      std::fflush(m_file_handle);
    }

    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_writing = false;
      if (m_tasks.empty()) {
        m_drained_condition.notify_all();
      }
    }
  }

  if (m_file_handle != nullptr) {
    std::fflush(m_file_handle);
  }
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_writing = false;
    m_drained_condition.notify_all();
  }
}

bool AsyncLogger::openLogFile(const std::string& date) {
  if (date != m_date) {
    closeFile();
    m_date = date;
    m_file_no = 0;
  }
  if (m_file_handle != nullptr) {
    return true;
  }

  while (true) {
    const std::string file_name = makeFileName();
    m_file_handle = std::fopen(file_name.c_str(), "a+");
    if (m_file_handle == nullptr) {
      std::cerr << "open log file failed: " << file_name
                << ", error=" << std::strerror(errno) << std::endl;
      return false;
    }
    if (std::fseek(m_file_handle, 0, SEEK_END) != 0) {
      std::cerr << "seek log file failed: " << file_name << std::endl;
      closeFile();
      return false;
    }
    const long position = std::ftell(m_file_handle);
    m_file_size = position > 0 ? static_cast<std::size_t>(position) : 0;
    if (m_max_size == 0 || m_file_size < m_max_size) {
      return true;
    }
    closeFile();
    ++m_file_no;
  }
}

bool AsyncLogger::rotateIfNeeded(std::size_t incoming_size) {
  if (m_file_handle == nullptr) {
    return false;
  }
  if (m_max_size == 0 || m_file_size == 0 ||
      m_file_size + incoming_size <= m_max_size) {
    return true;
  }

  closeFile();
  ++m_file_no;
  return openLogFile(m_date);
}

std::string AsyncLogger::makeFileName() const {
  std::ostringstream stream;
  stream << m_file_path << m_file_name << '_' << m_date << '_'
         << LogTypeToString(m_log_type) << '_' << m_file_no << ".log";
  return stream.str();
}

void AsyncLogger::closeFile() {
  if (m_file_handle != nullptr) {
    std::fflush(m_file_handle);
    std::fclose(m_file_handle);
    m_file_handle = nullptr;
  }
  m_file_size = 0;
}

Logger& Logger::GetInstance() {
  static Logger logger;
  return logger;
}

Logger* Logger::GetLogger() {
  return &GetInstance();
}

Logger::~Logger() {
  shutdown();
}

void Logger::init(const Config& config) {
  std::lock_guard<std::mutex> lock(m_state_mutex);
  if (m_initialized.load()) {
    return;
  }

  m_sync_interval = config.m_log_sync_interval;
  m_rpc_level = config.m_log_level;
  m_app_level = config.m_app_log_level;
  m_stopping.store(false);
  m_async_rpc_logger = std::make_shared<AsyncLogger>(
      config.m_log_prefix, config.m_log_path,
      config.m_log_max_file_size, LogType::RPC_LOG);
  m_async_app_logger = std::make_shared<AsyncLogger>(
      config.m_log_prefix, config.m_log_path,
      config.m_log_max_file_size, LogType::APP_LOG);
  m_initialized.store(true);
  m_sync_thread = std::thread(&Logger::syncLoop, this);
}

void Logger::pushRpcLog(const std::string& log_msg) {
  if (!m_initialized.load() || m_stopping.load()) {
    return;
  }
  std::lock_guard<std::mutex> lock(m_rpc_buffer_mutex);
  m_rpc_buffer.push_back(log_msg);
}

void Logger::pushAppLog(const std::string& log_msg) {
  if (!m_initialized.load() || m_stopping.load()) {
    return;
  }
  std::lock_guard<std::mutex> lock(m_app_buffer_mutex);
  m_app_buffer.push_back(log_msg);
}

void Logger::dispatchBuffers() {
  std::vector<std::string> rpc_buffer;
  std::vector<std::string> app_buffer;
  {
    std::lock_guard<std::mutex> lock(m_rpc_buffer_mutex);
    rpc_buffer.swap(m_rpc_buffer);
  }
  {
    std::lock_guard<std::mutex> lock(m_app_buffer_mutex);
    app_buffer.swap(m_app_buffer);
  }

  if (m_async_rpc_logger != nullptr) {
    m_async_rpc_logger->push(rpc_buffer);
  }
  if (m_async_app_logger != nullptr) {
    m_async_app_logger->push(app_buffer);
  }
}

void Logger::flush() {
  if (!m_initialized.load()) {
    return;
  }
  dispatchBuffers();
  if (m_async_rpc_logger != nullptr) {
    m_async_rpc_logger->flush();
  }
  if (m_async_app_logger != nullptr) {
    m_async_app_logger->flush();
  }
}

void Logger::shutdown() {
  {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    if (!m_initialized.load() || m_stopping.exchange(true)) {
      return;
    }
  }

  m_sync_condition.notify_all();
  if (m_sync_thread.joinable()) {
    m_sync_thread.join();
  }

  dispatchBuffers();
  if (m_async_rpc_logger != nullptr) {
    m_async_rpc_logger->stop();
  }
  if (m_async_app_logger != nullptr) {
    m_async_app_logger->stop();
  }

  std::lock_guard<std::mutex> lock(m_state_mutex);
  m_async_rpc_logger.reset();
  m_async_app_logger.reset();
  m_initialized.store(false);
}

bool Logger::isInitialized() const {
  return m_initialized.load() && !m_stopping.load();
}

bool Logger::shouldLog(LogLevel level, LogType type) const {
  if (!isInitialized()) {
    return false;
  }
  const LogLevel threshold =
      type == LogType::APP_LOG ? m_app_level : m_rpc_level;
  return static_cast<int>(level) >= static_cast<int>(threshold) &&
         threshold != LogLevel::NONE;
}

void Logger::syncLoop() {
  std::unique_lock<std::mutex> lock(m_sync_mutex);
  while (!m_stopping.load()) {
    if (m_sync_condition.wait_for(
            lock, std::chrono::milliseconds(m_sync_interval),
            [this]() { return m_stopping.load(); })) {
      break;
    }
    lock.unlock();
    dispatchBuffers();
    lock.lock();
  }
}

LogEvent::LogEvent(LogLevel level, const char* file_name, int line,
                   const char* func_name, LogType type)
    : m_level(level),
      m_file_name(file_name),
      m_line(line),
      m_func_name(func_name),
      m_type(type),
      m_enabled(ShouldLog(level, type)) {}

LogEvent::~LogEvent() {
  if (!m_enabled) {
    return;
  }
  appendPrefix();
  m_stream << '\n';
  if (m_type == LogType::APP_LOG) {
    Logger::GetInstance().pushAppLog(m_stream.str());
  } else {
    Logger::GetInstance().pushRpcLog(m_stream.str());
  }
}

std::stringstream& LogEvent::getStringStream() {
  if (m_enabled) {
    appendPrefix();
  }
  return m_stream;
}

void LogEvent::appendPrefix() {
  if (m_prefix_appended) {
    return;
  }
  m_prefix_appended = true;

  timeval now {};
  gettimeofday(&now, nullptr);
  tm local_time {};
  localtime_r(&now.tv_sec, &local_time);
  char time_buffer[32] = {0};
  strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S",
           &local_time);

  Coroutine* coroutine = Coroutine::GetCurrentCoroutine();
  const int coroutine_id = coroutine == nullptr ? 0 : coroutine->getCorId();

  m_stream << '[' << time_buffer << '.' << std::setw(6) << std::setfill('0')
           << now.tv_usec << "][" << LogLevelToString(m_level) << "]["
           << getpid() << "][" << CurrentThreadId() << "]["
           << coroutine_id << "][" << BaseName(m_file_name) << ':' << m_line
           << ':' << (m_func_name == nullptr ? "" : m_func_name) << ']';

  RunTime* runtime = getCurrentRunTime();
  if (runtime != nullptr) {
    if (!runtime->m_msg_no.empty()) {
      m_stream << '[' << runtime->m_msg_no << ']';
    }
    if (!runtime->m_interface_name.empty()) {
      m_stream << '[' << runtime->m_interface_name << ']';
    }
  }
}

void InitLogger() {
  Logger::GetInstance().init(*GetConfig());
}

void FlushLogger() {
  Logger::GetInstance().flush();
}

void ShutdownLogger() {
  Logger::GetInstance().shutdown();
}

bool OpenLog() {
  return Logger::GetInstance().isInitialized();
}

bool ShouldLog(LogLevel level, LogType type) {
  return Logger::GetInstance().shouldLog(level, type);
}

}  // namespace crpc
