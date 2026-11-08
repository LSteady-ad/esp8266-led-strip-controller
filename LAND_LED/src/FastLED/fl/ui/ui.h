#pragma once

/// @file fl/ui/ui.h
/// @brief Aggregator header for the fl/ui/ family of per-element UI types.
///
/// Each UI type lives in its own header under fl/ui/. This file re-exports
/// them so consumers can include the entire UI surface with one include.

#include "audio.h"
#include "button.h"
#include "checkbox.h"
#include "description.h"
#include "dropdown.h"
#include "element.h"
#include "group.h"
#include "help.h"
#include "number_field.h"
#include "slider.h"
#include "title.h"
