#pragma once

// Caleido3 visualizer class
// Extracted from animartrix_detail.hpp

#include "../fp_state.h"
#include "viz_base.h"

namespace fl {

class Caleido3 : public IAnimartrix2Viz {
public:
    void draw(Context &ctx) override;
};


// Fixed-point Q31 scalar implementation of Caleido3.
class Caleido3_FP : public IAnimartrix2Viz {
public:
    void draw(Context &ctx) override;
private:
    FPVizState mState;
};

}  // namespace fl
