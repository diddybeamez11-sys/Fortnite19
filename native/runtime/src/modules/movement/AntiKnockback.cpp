#include "modules/movement/AntiKnockback.h"

#include "modules/ModuleManager.h"
#include <android/log.h>

namespace eclient_runtime::modules::movement {

bool AntiKnockback::enabled() {
    return ModuleManager::instance().enabled("AntiKnockback");
}

void AntiKnockback::update(void* player) {
    if (!player || !enabled()) return;
    __android_log_print(ANDROID_LOG_INFO, "EClientMovement", "AntiKnockback armed");
}

} // namespace eclient_runtime::modules::movement
