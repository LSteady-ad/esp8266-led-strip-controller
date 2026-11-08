#pragma once

// Slow_Fade visualizer class
// Extracted from animartrix_detail.hpp

#include "../fp_state.h"
#include "viz_base.h"

namespace fl {

class Slow_Fade : public IAnimartrix2Viz {
public:
    void draw(Context &ctx) override;
};


// Fixed-point Q31 scalar implementation of Slow_Fade.
class Slow_Fade_FP : public IAnimartrix2Viz {
public:
    void draw(Context &ctx) override;
private:
    FPVizState mState;
};

}  // namespace fl
