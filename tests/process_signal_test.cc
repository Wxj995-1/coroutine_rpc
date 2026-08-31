#include <signal.h>

#include "comm/process_signal.h"

int main() {
  if (!crpc::PrepareProcessSignals()) {
    return 1;
  }

  // SIGPIPE must not terminate the process after framework initialization.
  return ::raise(SIGPIPE) == 0 ? 0 : 2;
}
