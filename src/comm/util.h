#pragma once
#include <sys/syscall.h>
#include <unistd.h>

namespace crpc {

inline pid_t gettid() {
  return static_cast<pid_t>(syscall(SYS_gettid));
}

}  // namespace crpc
