// ok no namespace fl
#pragma once

// IWYU pragma: private


#include "../wasm/is_wasm.h"
#ifdef FL_IS_WASM
#include "../wasm/fastspi_wasm.h"
#elif defined(FASTLED_STUB_IMPL)
#include "fastspi_stub_generic.h"
#else
#error "This file should only be included for stub or emscripten builds"
#endif