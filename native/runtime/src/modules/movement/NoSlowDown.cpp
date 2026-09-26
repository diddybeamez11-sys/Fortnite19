#include "modules/movement/NoSlowDown.h"

#include "modules/ModuleManager.h"
#include <android/log.h>

namespace eclient_runtime::modules::movement {

bool NoSlowDown::enabled() {
    return ModuleManager::instance().enabled("NoSlowDown");
}

void NoSlowDown::update(void* player) {
    if (!player || !enabled()) return;
    __android_log_print(ANDROID_LOG_INFO, "EClientMovement", "NoSlowDown armed");
}

} // namespace eclient_runtime::modules::movement
