#include "modules/player/NoFire.h"

#include "modules/ModuleManager.h"
#include <android/log.h>

namespace eclient_runtime::modules::player {

bool NoFire::enabled() {
    return ModuleManager::instance().enabled("NoFire");
}

void NoFire::update(void* player) {
    if (!player || !enabled()) return;
    __android_log_print(ANDROID_LOG_INFO, "EClientPlayer", "NoFire armed");
}

} // namespace eclient_runtime::modules::player
