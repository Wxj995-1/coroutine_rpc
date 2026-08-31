#include "comm/process_signal.h"

#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include <cstdlib>
#include <mutex>
#include <thread>

#include "comm/log.h"

namespace crpc {
namespace {

sigset_t g_shutdown_signals;
std::once_flag g_prepare_once;
std::once_flag g_start_once;
bool g_prepare_succeeded = false;
bool g_start_succeeded = false;

void FatalSignalHandler(int signal_no) {
  // Only async-signal-safe functions are allowed here. In particular, do not
  // use Logger, malloc, mutexes, condition variables, or stdio.
  static const char message[] =
      "coroutine_rpc received a fatal signal; core dump follows\n";
  const ssize_t ignored =
      ::write(STDERR_FILENO, message, sizeof(message) - 1);
  (void)ignored;

  // SA_RESETHAND restores the default disposition before this handler runs;
  // SA_NODEFER lets the re-raised signal take effect immediately.
  if (::raise(signal_no) != 0) {
    ::_exit(128 + signal_no);
  }
}

bool InstallSignalAction(int signal_no, void (*handler)(int), int flags) {
  struct sigaction action {};
  action.sa_handler = handler;
  action.sa_flags = flags;
  if (::sigemptyset(&action.sa_mask) != 0) {
    return false;
  }
  return ::sigaction(signal_no, &action, nullptr) == 0;
}

void SignalWaitLoop() {
  int signal_no = 0;
  const int result = ::sigwait(&g_shutdown_signals, &signal_no);
  if (result != 0) {
    static const char message[] =
        "coroutine_rpc failed while waiting for a shutdown signal\n";
    const ssize_t ignored =
        ::write(STDERR_FILENO, message, sizeof(message) - 1);
    (void)ignored;
    ::_exit(EXIT_FAILURE);
  }

  AppWarnLog("received shutdown signal, signal=%d", signal_no);
  ShutdownLogger();
  ::_exit(128 + signal_no);
}

}  // namespace

bool PrepareProcessSignals() {
  std::call_once(g_prepare_once, []() {
    if (!InstallSignalAction(SIGPIPE, SIG_IGN, 0) ||
        !InstallSignalAction(SIGSEGV, FatalSignalHandler,
                             SA_RESETHAND | SA_NODEFER) ||
        !InstallSignalAction(SIGABRT, FatalSignalHandler,
                             SA_RESETHAND | SA_NODEFER)) {
      return;
    }

    if (::sigemptyset(&g_shutdown_signals) != 0 ||
        ::sigaddset(&g_shutdown_signals, SIGINT) != 0 ||
        ::sigaddset(&g_shutdown_signals, SIGTERM) != 0) {
      return;
    }

    if (::pthread_sigmask(SIG_BLOCK, &g_shutdown_signals, nullptr) != 0) {
      return;
    }
    g_prepare_succeeded = true;
  });
  return g_prepare_succeeded;
}

bool StartSignalWaiter() {
  if (!g_prepare_succeeded) {
    return false;
  }

  std::call_once(g_start_once, []() {
    try {
      std::thread waiter(SignalWaitLoop);
      waiter.detach();
      g_start_succeeded = true;
    } catch (...) {
      g_start_succeeded = false;
    }
  });
  return g_start_succeeded;
}

}  // namespace crpc
