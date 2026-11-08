// IWYU pragma: private

/// @file _build.hpp
/// @brief Unity build header for platforms\arm\stm32/ directory
/// Includes all implementation files in alphabetical order
///
/// Includes SPI hardware drivers from drivers/ subdirectory

// begin current directory includes
#include "init_channel_driver_stm32.cpp.hpp"
#include "init_stm32.cpp.hpp"
#include "io_stm32.cpp.hpp"
#include "mutex_stm32.cpp.hpp"
#include "semaphore_stm32.cpp.hpp"
#include "stm32_gpio_timer_helpers.cpp.hpp"

// begin sub directory includes
#include "drivers/_build.cpp.hpp"
