// IWYU pragma: private

/// @file _build.hpp
/// @brief Unity build header for platforms\esp\32/ directory
/// Includes all implementation files in alphabetical order

// Root directory implementations (alphabetical order)

// begin current directory includes
#include "condition_variable_esp32.cpp.hpp"
#include "init_esp32.cpp.hpp"
#include "io_esp.cpp.hpp"
#include "lwip_hooks.cpp.hpp"
#include "mutex_esp32.cpp.hpp"
#include "platform_time_esp32.cpp.hpp"
#include "semaphore_esp32.cpp.hpp"
#include "watchdog_esp32.cpp.hpp"

// begin sub directory includes
#include "audio/_build.cpp.hpp"
#include "codec/_build.cpp.hpp"
#include "core/_build.cpp.hpp"
#include "drivers/_build.cpp.hpp"
#include "interrupts/_build.cpp.hpp"
#include "ota/_build.cpp.hpp"
