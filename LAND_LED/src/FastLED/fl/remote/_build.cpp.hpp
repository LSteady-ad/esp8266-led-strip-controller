/// @file remote/_build.hpp
/// @brief Unity build header for fl/remote/ directory
/// Includes in dependency order: base types, transport, rpc subsystem, then remote

// begin current directory includes
#include "remote.cpp.hpp"
#include "types.cpp.hpp"

// begin sub directory includes
#include "rpc/_build.cpp.hpp"
#include "transport/_build.cpp.hpp"
