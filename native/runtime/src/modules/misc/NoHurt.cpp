#include "modules/misc/NoHurt.h"

#include "modules/ModuleManager.h"
#include <android/log.h>

namespace eclient_runtime::modules::misc {

bool NoHurt::enabled() {
    return ModuleManager::instance().enabled("NoHurt");
}

void NoHurt::update(void* player) {
    if (!player || !enabled()) return;
    __android_log_print(ANDROID_LOG_INFO, "EClientMisc", "NoHurt armed");
}

} // namespace eclient_runtime::modules::misc
