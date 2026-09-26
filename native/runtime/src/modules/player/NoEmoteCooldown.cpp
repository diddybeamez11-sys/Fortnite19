#include "modules/player/NoEmoteCooldown.h"

#include "modules/ModuleManager.h"
#include <android/log.h>

namespace eclient_runtime::modules::player {

bool NoEmoteCooldown::enabled() {
    return ModuleManager::instance().enabled("NoEmoteCooldown");
}

void NoEmoteCooldown::update(void* player) {
    if (!player || !enabled()) return;
    __android_log_print(ANDROID_LOG_INFO, "EClientPlayer", "NoEmoteCooldown armed");
}

} // namespace eclient_runtime::modules::player
