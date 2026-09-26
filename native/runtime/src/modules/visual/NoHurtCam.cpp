#include "modules/visual/NoHurtCam.h"

#include "modules/ModuleManager.h"
#include <android/log.h>

namespace eclient_runtime::modules::visual {

bool NoHurtCam::enabled() {
    return ModuleManager::instance().enabled("NoHurtCam");
}

void NoHurtCam::update(void* player) {
    if (!player || !enabled()) return;
    __android_log_print(ANDROID_LOG_INFO, "EClientVisual", "NoHurtCam armed");
}

} // namespace eclient_runtime::modules::visual
