/// @file _build.hpp
/// @brief Unity build header for fl/ directory
/// Includes all implementation files in alphabetical order

// Note: Teensy 3.x <new.h> compatibility is handled in _build.cpp
// which includes is_teensy.h before this header.

// begin sub directory includes
#include "asset/_build.cpp.hpp"
#include "audio/_build.cpp.hpp"
#include "channels/_build.cpp.hpp"
#include "chipsets/_build.cpp.hpp"
#include "codec/_build.cpp.hpp"
#include "control/_build.cpp.hpp"
#include "font/_build.cpp.hpp"
#include "fx/_build.cpp.hpp"
#include "gfx/_build.cpp.hpp"
#include "log/_build.cpp.hpp"
#include "math/_build.cpp.hpp"
#include "net/_build.cpp.hpp"
#include "remote/_build.cpp.hpp"
#include "sensors/_build.cpp.hpp"

#include "stl/_build.cpp.hpp"
#include "system/_build.cpp.hpp"
#include "task/_build.cpp.hpp"
#include "test/_build.cpp.hpp"
#include "ui/_build.cpp.hpp"
#include "video/_build.cpp.hpp"
#include "wdt/_build.cpp.hpp"
