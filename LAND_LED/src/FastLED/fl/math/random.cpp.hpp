#include "random.h"
#include "../stl/singleton.h"
#include "../stl/mutex.h"

namespace fl {

namespace {

struct LockedRandom {
    fl::mutex mtx;
    math::random rng;
};

} // namespace

math::random& default_random() {
    return Singleton<LockedRandom>::instance().rng;
}

} // namespace fl
