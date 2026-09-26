#include "modules/movement/Fly.h"

#include "modules/ModuleManager.h"
#include <android/log.h>

namespace eclient_runtime::modules::movement {

bool Fly::enabled() {
    return ModuleManager::instance().enabled("Fly");
}

void Fly::update(void* player) {
    if (!player || !enabled()) return;
    __android_log_print(ANDROID_LOG_INFO, "EClientMovement", "Fly armed");
}

} // namespace eclient_runtime::modules::movement
