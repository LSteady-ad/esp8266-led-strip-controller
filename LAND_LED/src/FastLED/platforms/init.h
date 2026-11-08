// ok no namespace fl
#pragma once

/// @file platforms/init.h
/// @brief Platform dispatch for one-time initialization
///
/// This header provides platform-specific initialization functions in fl::platforms::
/// namespace. It follows the coarse-to-fine delegation pattern, routing to
/// platform-specific headers based on platform detection.
///
/// The initialization function is called once during FastLED::init() to perform
/// any platform-specific setup required before LED operations begin.
///
/// Supported platforms:
/// - ESP32: Initialize channel bus manager and SPI system
/// - ESP8266: Initialize UART driver (minimal)
/// - Teensy 4.x: Initialize ObjectFLED registry
/// - RP2040/RP2350: Initialize PIO parallel group
/// - STM32: Initialize SPI hardware controllers
/// - SAMD21/SAMD51: Initialize SPI hardware controllers
/// - nRF52: Initialize SPI hardware controllers
/// - WASM: Initialize driver listener
/// - Windows: Initialize Winsock
/// - Silicon Labs/Renesas: Initialize GPIO system
/// - Other platforms: Use stub implementation (no-op)
///
/// This header is included from FastLED.h and called during library initialization.

#include "is_platform.h"

// Platform dispatch (coarse-to-fine detection)
#ifdef FL_IS_ESP32
    #include "esp/32/init_esp32.h"
#elif defined(FL_IS_ESP8266)
    #include "esp/8266/init_esp8266.h"
#elif defined(FL_IS_TEENSY_4X)
    #include "arm/teensy/init_teensy4.h"
#elif defined(FL_IS_RP)
    #include "arm/rp/init_rp.h"
#elif defined(FL_IS_STM32)
    #include "arm/stm32/init_stm32.h"
#elif defined(FL_IS_SAMD51)
    #include "arm/d51/init_samd51.h"
#elif defined(FL_IS_SAMD21)
    #include "arm/d21/init_samd21.h"
#elif defined(FL_IS_NRF52)
    #include "arm/nrf52/init_nrf52.h"
#elif defined(FL_IS_WASM)
    #include "wasm/init_wasm.h"
#elif defined(FL_IS_WIN)
    #include "win/init_win.h"
#elif defined(FL_IS_SILABS_MGM240)
    #include "arm/mgm240/init_mgm240.h"
#elif defined(FL_IS_SILABS)
    #include "arm/silabs/init_silabs.h"
#elif defined(FL_IS_RENESAS)
    #include "arm/renesas/init_renesas.h"
#elif defined(FL_IS_SAM)
    #include "arm/sam/init_sam.h"
#elif defined(FL_IS_APOLLO3)
    #include "apollo3/init_apollo3.h"
#elif defined(FL_IS_AVR)
    #include "avr/init_avr.h"
#elif defined(FL_IS_POSIX)
    #include "posix/init_posix.h"
#else
    // Default fallback: Use stub implementation for all platforms
    #include "stub/init_stub.h"
#endif
