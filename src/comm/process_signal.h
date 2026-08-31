#pragma once

namespace crpc {

// Must be called before creating logger, reactor, or IO threads.
bool PrepareProcessSignals();

// Must be called after the logger has been initialized.
bool StartSignalWaiter();

}  // namespace crpc
