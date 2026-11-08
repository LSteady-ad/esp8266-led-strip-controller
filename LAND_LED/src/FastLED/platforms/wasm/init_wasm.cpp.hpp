// IWYU pragma: private

/// @file init_wasm.cpp
/// @brief WebAssembly platform initialization implementation
///
/// This file provides the implementation of platform initialization for WASM.
/// It initializes the driver listener system for JavaScript runtime integration.

#include "../../fl/stl/compiler_control.h"
#include "is_wasm.h"

#ifdef FL_IS_WASM

#include "init_wasm.h"
#include "../../fl/log/log.h"

// Include driver listener header
#include "engine_listener.h"

// Include channel driver initialization
#include "init_channel_driver.h"

namespace fl {
namespace platforms {

/// @brief Initialize WebAssembly platform
///
/// Performs one-time initialization of WASM-specific subsystems.
/// This function uses a static flag to ensure initialization happens only once.
void init() {
    static bool initialized = false;

    if (initialized) {
        return;  // Already initialized
    }

    FL_DBG("WASM: Platform initialization starting");

    // Initialize driver listener system
    // This connects FastLED driver events to the JavaScript runtime
    EngineListener::Init();

    // Initialize channel drivers for WASM platform
    // This registers the stub driver with ChannelManager
    platforms::initChannelDrivers();

    initialized = true;
    FL_DBG("WASM: Platform initialization complete");
}

} // namespace platforms
} // namespace fl

#endif // FL_IS_WASM
