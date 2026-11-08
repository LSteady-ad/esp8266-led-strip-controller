#pragma once

#include "../../stl/stdint.h"
#include "../../stl/string.h"
#include "../../stl/vector.h"
#include "../../stl/span.h"

namespace fl {

// Base64 encode/decode utilities for binary data transport.
// Used by JSON-RPC to transfer binary blobs as base64 strings.

// Encode binary data to a base64 string.
fl::string base64_encode(fl::span<const fl::u8> data);

// Decode a base64 string back to binary data.
// Returns empty vector on invalid input.
fl::vector<fl::u8> base64_decode(const fl::string& encoded);

} // namespace fl
