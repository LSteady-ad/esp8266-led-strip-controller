// ok no header
#define FASTLED_INTERNAL
#include "fastled.h"
#include "../../cled_controller.h"
#include "../stl/noexcept.h"

namespace fl {
fl::u16 cled_contoller_size() FL_NOEXCEPT {
	return sizeof(CLEDController);
}
}  // namespace fl
