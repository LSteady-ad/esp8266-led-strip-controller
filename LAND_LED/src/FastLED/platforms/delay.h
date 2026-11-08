#pragma once

#ifndef __INC_FASTLED_PLATFORMS_DELAY_H
#define __INC_FASTLED_PLATFORMS_DELAY_H

/// @file platforms/delay.h
/// Central distribution header for platform-specific delay implementations
/// This header handles all platform detection and includes the appropriate
/// platform-specific delay and delay_cycles headers.

#include "cycle_type.h"
#include "cpu_frequency.h"
#include "../fl/stl/compiler_control.h"
#include "is_platform.h"

// ============================================================================
// Platform-specific cycle delay includes (delaycycles)
// ============================================================================
// These provide NOP macros and delaycycles template implementations
// Must be included BEFORE platform-specific delay headers that use delay_cycles functions

#include "delaycycles.h"

// ============================================================================
// Platform-specific nanosecond-precision delay includes
// ============================================================================

#if defined(ARDUINO_ARCH_AVR)
#include "avr/delay.h"
#elif defined(ESP32) && !defined(ESP32C3) && !defined(ESP32C6)
#include "esp/32/core/delay.h"
#elif defined(ESP32C3) || defined(ESP32C6)
#include "esp/32/core/delay_riscv.h"
#elif defined(ARDUINO_ARCH_RP2040)
#include "arm/rp/rp2040/delay.h"
#elif defined(NRF52_SERIES)
#include "arm/nrf52/delay.h"
#elif defined(ARDUINO_ARCH_SAMD)
#include "arm/d21/delay.h"
#elif defined(FL_IS_TEENSY)
// Teensy uses generic ARM delay - DWT cycle counter works on Cortex-M4/M7
#include "delay_generic.h"
#elif defined(FL_IS_STM32)
// STM32 has specific DWT-based delay implementation
#include "arm/stm32/delay.h"
#elif defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__)
// Other ARM Cortex-M3/M4/M7 platforms - use generic delay
#include "delay_generic.h"
#elif defined(FASTLED_STUB_IMPL)
#include "stub/delay.h"
#else
#include "delay_generic.h"
#include "../fl/stl/noexcept.h"
#endif

namespace fl {

// Forward declarations for platform-specific delay implementations
/// Platform-specific implementation of nanosecond delay with runtime frequency
/// @param ns Number of nanoseconds
/// @param hz CPU frequency in Hz
FASTLED_FORCE_INLINE void delayNanoseconds_impl(u32 ns, u32 hz) FL_NOEXCEPT;

/// Platform-specific implementation of nanosecond delay with auto-detected frequency
/// @param ns Number of nanoseconds
FASTLED_FORCE_INLINE void delayNanoseconds_impl(u32 ns) FL_NOEXCEPT;

}  // namespace fl

#endif  // __INC_FASTLED_PLATFORMS_DELAY_H
