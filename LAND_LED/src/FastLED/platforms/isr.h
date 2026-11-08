/*
  FastLED — Cross-Platform ISR Platform Dispatch Header
  -----------------------------------------------------
  Dispatches ISR implementation to platform-specific headers.
  Each platform header provides inline implementations that call fl::platforms::* functions.

  Uses standardized FL_IS_* macros from is_platform.h for platform detection.

  License: MIT (FastLED)
*/

#pragma once

#include "is_platform.h"

// ok no namespace fl - dispatch header includes platform-specific implementations

// Platform detection using FL_IS_* macros
#if defined(FL_IS_STUB)
    #include "stub/isr_stub.hpp"
#elif defined(FL_IS_WASM)
    #include "wasm/isr_wasm.hpp"
#elif defined(FL_IS_ESP32)
    #include "esp/32/isr_esp32.hpp"
#elif defined(FL_IS_TEENSY)
    #include "arm/teensy/isr_teensy.hpp"
#elif defined(FL_IS_STM32)
    #include "arm/stm32/isr_stm32.hpp"
#elif defined(FL_IS_NRF52)
    #include "arm/nrf52/isr_nrf52.hpp"
#elif defined(FL_IS_AVR)
    // AVR platform: Only ATmega chips have Timer1 hardware support
    // ATtiny chips fall back to null implementation (no compatible Timer1)
    #include "avr/isr_avr.hpp"
    // If isr_avr.hpp didn't define the implementation (ATtiny case), include null
    #ifndef FL_ISR_AVR_IMPLEMENTED
        #include "isr_null.hpp"
    #endif
#elif defined(FL_IS_RP)
    #include "arm/rp/isr_rp.hpp"
#elif defined(FL_IS_SAMD)
    #include "arm/samd/isr_samd.hpp"
#elif defined(FL_IS_SAM)
    #include "arm/sam/isr_sam.hpp"
#else
    #include "isr_null.hpp"
#endif
