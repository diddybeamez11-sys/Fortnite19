#include "modules/visual/FullBright.h"

#include "modules/ModuleManager.h"
#include <android/log.h>

namespace eclient_runtime::modules::visual {

bool FullBright::enabled() {
    return ModuleManager::instance().enabled("FullBright");
}

void FullBright::update(void* player) {
    if (!player || !enabled()) return;
    __android_log_print(ANDROID_LOG_INFO, "EClientVisual", "FullBright armed");
}

} // namespace eclient_runtime::modules::visual
