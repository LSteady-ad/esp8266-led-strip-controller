#pragma once

// Module_Experiment4 visualizer class
// Extracted from animartrix_detail.hpp

#include "../fp_state.h"
#include "viz_base.h"

namespace fl {

class Module_Experiment4 : public IAnimartrix2Viz {
public:
    void draw(Context &ctx) override;
};


// Fixed-point Q31 scalar implementation of Module_Experiment4.
class Module_Experiment4_FP : public IAnimartrix2Viz {
public:
    void draw(Context &ctx) override;
private:
    FPVizState mState;
};

}  // namespace fl
