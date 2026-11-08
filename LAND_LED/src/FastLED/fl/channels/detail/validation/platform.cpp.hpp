// src/fl/channels/detail/validation/platform.cpp.hpp
//
// Platform-specific validation implementation
// Queries ChannelManager for registered drivers instead of maintaining a
// hardcoded list.

#include "platform.h"
#include "../../manager.h"
#include "../../../stl/compiler_control.h"
#include "../../../log/log.h"

namespace fl {
namespace validation {

bool validateExpectedEngines() {
    auto infos = channelManager().getDriverInfos();
    return !infos.empty();
}

void printEngineValidation() {
    auto infos = channelManager().getDriverInfos();

    if (infos.empty()) {
        FL_ERROR("[VALIDATION] No drivers registered with ChannelManager!");
        return;
    }

    FL_WARN("\n[VALIDATION] Registered drivers: " << infos.size());
    for (fl::size i = 0; i < infos.size(); i++) {
        const auto& info = infos[i];
        FL_UNUSED(info);  // silences -Wunused-variable when FL_WARN is a no-op
        FL_WARN("  - " << info.name.c_str()
                        << " (priority=" << info.priority
                        << ", enabled=" << (info.enabled ? "true" : "false")
                        << ")");
    }
    FL_WARN("[VALIDATION] Driver registration OK");
}

}  // namespace validation
}  // namespace fl
