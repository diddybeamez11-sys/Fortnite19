#include "modules/visual/NoBlur.h"

#include "modules/ModuleManager.h"
#include <android/log.h>

namespace eclient_runtime::modules::visual {

bool NoBlur::enabled() {
    return ModuleManager::instance().enabled("NoBlur");
}

void NoBlur::update(void* player) {
    if (!player || !enabled()) return;
    __android_log_print(ANDROID_LOG_INFO, "EClientVisual", "NoBlur armed");
}

} // namespace eclient_runtime::modules::visual
