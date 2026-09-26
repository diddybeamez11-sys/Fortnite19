#include "modules/GameplayModules.h"
#include "modules/ModuleManager.h"
#include "profile/Offsets_1_21_111.h"
#include "memory/NativeMemory.h"
#include "dobby.h"
#include <android/log.h>
#include <cstdint>

namespace eclient_runtime::modules::gameplay {
namespace {
constexpr const char* TAG = "EClientGameplay";
using namespace eclient_runtime::profile::mc_1_21_111;

using NormalTickFn = void* (*)(void*);
NormalTickFn oldNormalTick = nullptr;
std::uintptr_t base = 0;

inline void* fn(std::uint64_t off) { return reinterpret_cast<void*>(base + off); }
inline bool on(const char* n) { return ModuleManager::instance().enabled(n); }

void* hookedNormalTick(void* player) {
    void* result = oldNormalTick ? oldNormalTick(player) : nullptr;
    if (!player) return result;

    // Sprint and step are simple LocalPlayer calls with verified 1.21.111 addresses.
    if (on("AlwaysSprint")) {
        using Fn = void (*)(void*, bool);
        reinterpret_cast<Fn>(fn(SetSprinting))(player, true);
    }
    if (on("Step")) {
        using Fn = void (*)(void*, float);
        reinterpret_cast<Fn>(fn(InitMaxAutoStep))(player, 1.0625f);
    }

    return result;
}
} // anonymous

bool initialize() {
    if (base != 0) return true;
    memory::ModuleInfo info{};
    if (!memory::NativeMemory::instance().locateModule("libminecraftpe.so", info)) return false;
    base = info.base;

    if (DobbyHook(fn(NormalTick), reinterpret_cast<void*>(hookedNormalTick), reinterpret_cast<void**>(&oldNormalTick)) != RS_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "NormalTick hook failed");
        return false;
    }
    // Speed stays unavailable until its calling convention and velocity write
    // are verified. Installing a no-op hook here creates unnecessary crash
    // surface and makes the UI claim a module works when it does not.
    __android_log_print(ANDROID_LOG_INFO, TAG, "1.21.111 gameplay hooks installed: Step/Sprint");
    return true;
}
void shutdown() {}
} // namespace eclient_runtime::modules::gameplay
