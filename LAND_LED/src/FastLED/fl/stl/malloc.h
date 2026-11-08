#pragma once

#include "stdint.h"
#include "noexcept.h"

namespace fl {
    // Function declarations for memory management
    void* malloc(size_t size) FL_NOEXCEPT;
    void free(void* ptr) FL_NOEXCEPT;
    void* calloc(size_t nmemb, size_t size) FL_NOEXCEPT;
    void* realloc(void* ptr, size_t new_size) FL_NOEXCEPT;
} // namespace fl
