#include "comm/config.h"
#include "mprpcapplication.h"
#include <stdlib.h>

namespace crpc {

static Config* g_config = nullptr;

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
  m_cor_stack_size = load_int("cor_stack_size", m_cor_stack_size);
  m_cor_pool_size = load_int("cor_pool_size", m_cor_pool_size);
  m_msg_req_len = load_int("msg_req_len", m_msg_req_len);
  m_max_connect_timeout = load_int("max_connect_timeout", m_max_connect_timeout);
  m_iothread_num = load_int("iothread_num", m_iothread_num);
  m_timewheel_bucket_num = load_int("timewheel_bucket_num", m_timewheel_bucket_num);
  m_timewheel_inteval = load_int("timewheel_inteval", m_timewheel_inteval);
}

}  // namespace crpc
