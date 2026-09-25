#include "fastsm/fastsm.hpp"

#if defined(__APPLE__) && __has_include("fastsm_build_commit.h")
#include "fastsm_build_commit.h"
#endif

namespace fastsm {

const char* version() {
    return "0.6.0";
}

const char* build_commit() {
#ifdef FASTSM_BUILD_COMMIT
    return FASTSM_BUILD_COMMIT;
#else
    return "";
#endif
}

} // namespace fastsm
