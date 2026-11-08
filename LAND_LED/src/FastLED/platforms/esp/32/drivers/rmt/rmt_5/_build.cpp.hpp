// IWYU pragma: private

/// @file _build.hpp
/// @brief Unity build header for platforms\esp\32\drivers\rmt\rmt_5/ directory
/// Includes all implementation files in alphabetical order

#include "buffer_pool.cpp.hpp"
#include "channel_driver_rmt.cpp.hpp"
#include "network_detector.cpp.hpp"
#include "network_state_tracker.cpp.hpp"
#include "rmt5_controller_lowlevel.cpp.hpp"
#include "rmt5_peripheral_esp.cpp.hpp"
#include "rmt_memory_manager.cpp.hpp"

// BusTraits<Bus::RMT> specialization (header-only, included for parse + ODR-keep).
#include "bus_traits.h"
