#pragma once

// IWYU pragma: private

#include "is_wasm.h"
#ifndef FL_IS_WASM
#error "This file should only be included in an Emscripten build"
#endif

// Include hardware SPI implementation (for APA102/dotstar chipsets)
#include "../stub/fastspi_stub_generic.h"

// Include channel-based SPI controller for WASM platform (for WS2812 over SPI)
#include "spi_channel_wasm.h"

namespace fl {

// Compatibility typedef for existing WASM code
using WasmSpiOutput = ClocklessSPI<0, TIMING_WS2812_800KHZ>;

} // namespace fl
