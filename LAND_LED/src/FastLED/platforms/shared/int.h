// ok no namespace fl
#pragma once

// IWYU pragma: private

// Desktop/generic platform integer type definitions
// Dispatches to platform-specific files for macOS, Linux, Windows, etc.
//
// IMPORTANT: macOS and Linux LP64 systems define u64 differently:
// - macOS: u64 is 'unsigned long long' (even in 64-bit mode)
// - Linux: u64 is 'unsigned long' (in 64-bit LP64 mode)
//
// See individual platform files for details.

#include "../posix/is_posix.h"
#include "../win/is_win.h"

#if defined(FL_IS_APPLE)
    // macOS (all versions)
    #include "int_macos.h"
#elif defined(FL_IS_WIN)
    // Windows (all versions)
    #include "int_windows.h"
#elif defined(FL_IS_LINUX) && (defined(__LP64__) || defined(_LP64))
    // Linux LP64 (64-bit Linux)
    #include "int_linux.h"
#else
    // Generic/32-bit/unknown platforms
    #include "int_generic.h"
#endif
