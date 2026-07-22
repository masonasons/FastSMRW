#include "fastsm/fastsm.hpp"

namespace fastsm {

const char* version() {
    return "0.5.0";
}

const char* build_commit() {
#ifdef FASTSM_BUILD_COMMIT
    return FASTSM_BUILD_COMMIT;
#else
    return "";
#endif
}

} // namespace fastsm
