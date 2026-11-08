
/// @file _build.hpp
/// @brief Unity build header for platforms/ directory
/// Includes all root-level implementations and subdirectory _build.hpp files

// Root directory implementations (alphabetical order)

// begin current directory includes
#include "channel_drivers.impl.cpp.hpp" // ok include cpp.hpp
#include "compile_test.cpp.hpp"
#include "coroutine.impl.cpp.hpp" // ok include cpp.hpp
#include "debug_setup.cpp.hpp"
#include "ota.cpp.hpp"
#include "stub_main.cpp.hpp"
#include "watchdog.impl.cpp.hpp" // ok include cpp.hpp

// begin sub directory includes
#include "adafruit/_build.cpp.hpp"
#include "apollo3/_build.cpp.hpp"
#include "arduino/_build.cpp.hpp"
#include "arm/_build.cpp.hpp"
#include "avr/_build.cpp.hpp"
#include "esp/_build.cpp.hpp"
#include "posix/_build.cpp.hpp"
#include "shared/_build.cpp.hpp"
#include "stub/_build.cpp.hpp"
#include "teensy/_build.cpp.hpp"
#include "wasm/_build.cpp.hpp"
#include "win/_build.cpp.hpp"
