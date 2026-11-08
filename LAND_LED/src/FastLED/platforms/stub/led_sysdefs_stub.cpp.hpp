// IWYU pragma: private



// ok no namespace fl
#ifdef FASTLED_STUB_IMPL  // Only use this if explicitly defined.

#include "led_sysdefs_stub.h"

#if defined(__EMSCRIPTEN__)
#include "../wasm/led_sysdefs_wasm.h"
#else
#include "generic/led_sysdefs_generic.hpp"
#endif


#endif  // FASTLED_STUB_IMPL
