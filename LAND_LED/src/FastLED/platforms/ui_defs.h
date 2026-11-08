#pragma once

#ifndef FASTLED_USE_JSON_UI
// Enable JSON UI for WASM or stub platform (but not both checks - WASM has priority)
#if defined(__EMSCRIPTEN__) || defined(FASTLED_STUB_IMPL)
#define FASTLED_USE_JSON_UI 1
#else
#define FASTLED_USE_JSON_UI 0
#endif
#endif  // FASTLED_USE_JSON_UI

#if FASTLED_USE_JSON_UI
// WASM-specific bindings (not needed for stub platform)
#ifdef __EMSCRIPTEN__
#include "wasm/js_bindings.h"
#endif

#include "shared/ui/json/button.h"
#include "shared/ui/json/checkbox.h"
#include "shared/ui/json/description.h"
#include "shared/ui/json/dropdown.h"
#include "shared/ui/json/help.h"
#include "shared/ui/json/number_field.h"
#include "shared/ui/json/slider.h"
#include "shared/ui/json/title.h"

// WASM uses direct audio implementation instead of JSON audio
#ifdef __EMSCRIPTEN__
#include "wasm/ui_audio_wasm.h"
#else
#include "shared/ui/json/audio.h"
#endif

namespace fl {

#define FASTLED_HAS_UI_BUTTON 1
#define FASTLED_HAS_UI_SLIDER 1
#define FASTLED_HAS_UI_CHECKBOX 1
#define FASTLED_HAS_UI_NUMBER_FIELD 1
#define FASTLED_HAS_UI_TITLE 1
#define FASTLED_HAS_UI_DESCRIPTION 1
#define FASTLED_HAS_UI_HELP 1
#define FASTLED_HAS_UI_AUDIO 1
#define FASTLED_HAS_UI_DROPDOWN 1

typedef JsonNumberFieldImpl UINumberFieldImpl;
typedef JsonSliderImpl UISliderImpl;
typedef JsonCheckboxImpl UICheckboxImpl;
typedef JsonButtonImpl UIButtonImpl;
typedef JsonTitleImpl UITitleImpl;
typedef JsonDescriptionImpl UIDescriptionImpl;
typedef JsonHelpImpl UIHelpImpl;
typedef JsonDropdownImpl UIDropdownImpl;

// WASM uses direct audio implementation, not JSON-based
#ifdef __EMSCRIPTEN__
typedef WasmAudioImpl UIAudioImpl;
#else
typedef JsonAudioImpl UIAudioImpl;
#endif

} // namespace fl

#else
// not defined for the system.
#endif // FASTLED_USE_JSON_UI
