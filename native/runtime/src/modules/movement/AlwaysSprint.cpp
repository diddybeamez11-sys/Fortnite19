#include "modules/movement/AlwaysSprint.h"

#include "modules/ModuleManager.h"
#include <android/log.h>

namespace eclient_runtime::modules::movement {

bool AlwaysSprint::enabled() {
    return ModuleManager::instance().enabled("AlwaysSprint");
}

void AlwaysSprint::update(void* player) {
    if (!player || !enabled()) return;
    __android_log_print(ANDROID_LOG_INFO, "EClientMovement", "AlwaysSprint armed");
}

} // namespace eclient_runtime::modules::movement
