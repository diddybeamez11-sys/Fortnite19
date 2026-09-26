#include "modules/movement/Step.h"

#include "modules/ModuleManager.h"
#include <android/log.h>

namespace eclient_runtime::modules::movement {

bool Step::enabled() {
    return ModuleManager::instance().enabled("Step");
}

void Step::update(void* player) {
    if (!player || !enabled()) return;
    __android_log_print(ANDROID_LOG_INFO, "EClientMovement", "Step armed");
}

} // namespace eclient_runtime::modules::movement
