#pragma once

// Zoom2 visualizer class
// Extracted from animartrix_detail.hpp

#include "../fp_state.h"
#include "viz_base.h"

namespace fl {

class Zoom2 : public IAnimartrix2Viz {
public:
    void draw(Context &ctx) override;
};


// Fixed-point Q31 scalar implementation of Zoom2.
class Zoom2_FP : public IAnimartrix2Viz {
public:
    void draw(Context &ctx) override;
private:
    FPVizState mState;
};

}  // namespace fl
