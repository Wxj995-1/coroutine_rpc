#pragma once
#include <string>

namespace crpc {

class Config {
 public:
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
