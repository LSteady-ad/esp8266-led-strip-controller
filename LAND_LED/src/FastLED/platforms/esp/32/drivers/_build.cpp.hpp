// IWYU pragma: private

/// @file _build.hpp
/// @brief Unity build header for platforms\esp\32\drivers/ directory
/// Includes all implementation files in alphabetical order
///
/// Post-#2428 (binary-size fix for #2420 / #2421):
///
/// Each LED-output driver subdirectory's `_build.cpp.hpp` only compiles its
/// implementation on platforms where the driver is the platform default
/// (PARLIO on P4/C6/H2/C5, RMT elsewhere, LCD_SPI on S3, I2S_SPI on dev).
/// All other drivers are pure opt-in via `fl::enableDrivers<fl::Bus::X>()`
/// or `FastLED.enableAllDrivers()`, which include the driver's `bus_traits.h`
/// at the call site and ODR-link the TU. The auxiliary subdirs (`ble`,
/// `gpio_isr_rx`, `rmt_rx`) are not channel drivers and stay unconditional.
///
/// Unconditional aggregator structure (one include per subdir, alphabetical)
/// is preserved to satisfy the unity-build linter.

// Root directory implementations (alphabetical order)

// begin current directory includes
#include "channel_manager_esp32.cpp.hpp"
#include "cled.cpp.hpp"
#include "spi_hw_manager_esp32.cpp.hpp"
#include "uart_esp32_idf.hpp"
#include "usb_serial_jtag_esp32_idf.hpp"

// begin sub directory includes
#include "ble/_build.cpp.hpp"
#include "gpio_isr_rx/_build.cpp.hpp"
#include "i2s/_build.cpp.hpp"
#include "i2s_spi/_build.cpp.hpp"
#include "lcd_cam/_build.cpp.hpp"
#include "lcd_spi/_build.cpp.hpp"
#include "parlio/_build.cpp.hpp"
#include "rmt/_build.cpp.hpp"
#include "rmt_rx/_build.cpp.hpp"
#include "spi/_build.cpp.hpp"
#include "uart/_build.cpp.hpp"
