// IWYU pragma: private


// ok no namespace fl
#include "../../fl/stl/has_include.h"

#if FL_HAS_INCLUDE(<Adafruit_NeoPixel.h>)
// IWYU pragma: begin_keep
#include <Adafruit_NeoPixel.h>
// IWYU pragma: end_keep
#if defined(NEO_RGBW)
#include "clockless_real.hpp"
#else
#include "clockless_fake.hpp"
#endif
#else
#include "clockless_fake.hpp"
#endif
