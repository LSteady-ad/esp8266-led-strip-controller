/// @file fl/stl/isr/critical_section.cpp.hpp
/// @brief critical_section implementation

#include "critical_section.h"
#include "../../../platforms/isr.h"
#include "../noexcept.h"

namespace fl {

void interrupt_disable() {
    interruptsDisable();
}

void interrupt_enable() {
    interruptsEnable();
}

namespace isr {

critical_section::critical_section() {
    fl::interrupt_disable();
}

critical_section::~critical_section() FL_NOEXCEPT {
    fl::interrupt_enable();
}

} // namespace isr
} // namespace fl
