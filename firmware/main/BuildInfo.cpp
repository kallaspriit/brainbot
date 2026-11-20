#include "BuildInfo.hpp"

#include "esp_app_desc.h"

// Set by main/CMakeLists.txt for this file only
#ifndef BUILD_COMMIT
#    define BUILD_COMMIT "unknown"
#endif

namespace BuildInfo {
const char* getVersion() {
    return esp_app_get_description()->version;
}

const char* getCommit() {
    return BUILD_COMMIT;
}
} // namespace BuildInfo
