// ok no namespace fl
#pragma once

/// @file platforms/channel_poll_signal.h
/// @brief Platform dispatch for channel-manager poll wake signals.

#include "is_platform.h"

#if defined(FL_IS_ESP32)
#include "esp/32/channel_poll_signal_esp32.h"
#else
#include "shared/channel_poll_signal.h"
#endif
