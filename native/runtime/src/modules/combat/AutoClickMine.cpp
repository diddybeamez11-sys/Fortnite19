#include "modules/combat/AutoClickMine.h"

#include "modules/ModuleManager.h"
#include <android/log.h>

namespace eclient_runtime::modules::combat {

bool AutoClickMine::enabled() {
    return ModuleManager::instance().enabled("AutoClickMine");
}

void AutoClickMine::update(void* player) {
    if (!player || !enabled()) return;
    __android_log_print(ANDROID_LOG_INFO, "EClientCombat", "AutoClickMine armed");
}

} // namespace eclient_runtime::modules::combat
