#include "modules/movement/Noclip.h"

#include "modules/ModuleManager.h"
#include <android/log.h>

namespace eclient_runtime::modules::movement {

bool Noclip::enabled() {
    return ModuleManager::instance().enabled("Noclip");
}

void Noclip::update(void* player) {
    if (!player || !enabled()) return;
    __android_log_print(ANDROID_LOG_INFO, "EClientMovement", "Noclip armed");
}

} // namespace eclient_runtime::modules::movement
