#pragma once

// This header defines the type aliases used by fl::string implementation
// It must be included after shared_ptr.h is fully defined to avoid circular dependencies

#include "../shared_ptr.h"
#include "../not_null.h"
#include "string_holder.h"

namespace fl {

// Define shared_ptr type for StringHolder
using StringHolderPtr = fl::shared_ptr<fl::StringHolder>;
using NotNullStringHolderPtr = fl::not_null<StringHolderPtr>;

} // namespace fl
