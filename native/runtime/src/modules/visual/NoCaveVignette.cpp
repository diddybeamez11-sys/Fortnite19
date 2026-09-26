#include "modules/visual/NoCaveVignette.h"

#include "modules/ModuleManager.h"
#include <android/log.h>

namespace eclient_runtime::modules::visual {

bool NoCaveVignette::enabled() {
    return ModuleManager::instance().enabled("NoCaveVignette");
}

void NoCaveVignette::update(void* player) {
    if (!player || !enabled()) return;
    __android_log_print(ANDROID_LOG_INFO, "EClientVisual", "NoCaveVignette armed");
}

} // namespace eclient_runtime::modules::visual
