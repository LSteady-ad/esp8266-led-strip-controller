// ok no namespace fl
#pragma once

// IWYU pragma: private

/// @file interrupts_stm32_mbed.h
/// Interrupt control for Arduino Mbed OS STM32 boards.

#include "../../../is_platform.h"

#ifndef FL_IS_STM32_MBED
#error "This header is only for Arduino Mbed OS STM32 boards"
#endif

#include "../interrupts_stm32_inline.h"
