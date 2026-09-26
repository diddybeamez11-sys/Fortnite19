#include "modules/GameplayModules.h"
#include "modules/ModuleManager.h"
#include "modules/movement/Fly.h"
#include "modules/misc/NoHurt.h"
#include "modules/player/NoFire.h"
#include "modules/combat/AutoClickMine.h"
#include "modules/movement/AlwaysSprint.h"
#include "modules/movement/Step.h"
#include "modules/movement/NoSlowDown.h"
#include "modules/movement/Noclip.h"
#include "modules/movement/AntiKnockback.h"
#include "modules/visual/FullBright.h"
#include "modules/visual/NoHurtCam.h"
#include "modules/visual/NoBlur.h"
#include "modules/visual/NoCaveVignette.h"
#include "modules/player/NoEmoteCooldown.h"
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
        eclient_runtime::modules::movement::AlwaysSprint::update(player);
    }
    if (on("Step")) {
        using Fn = void (*)(void*, float);
        reinterpret_cast<Fn>(fn(InitMaxAutoStep))(player, 1.0625f);
        eclient_runtime::modules::movement::Step::update(player);
    }
    if (on("NoSlowDown")) {
        eclient_runtime::modules::movement::NoSlowDown::update(player);
    }
    if (on("Noclip")) {
        eclient_runtime::modules::movement::Noclip::update(player);
    }
    if (on("AntiKnockback")) {
        eclient_runtime::modules::movement::AntiKnockback::update(player);
    }
    if (on("Fly") && player) {
        eclient_runtime::modules::movement::Fly::update(player);
    }
    if (on("AutoClickMine")) {
        eclient_runtime::modules::combat::AutoClickMine::update(player);
    }
    if (on("FullBright")) {
        eclient_runtime::modules::visual::FullBright::update(player);
    }
    if (on("NoHurtCam")) {
        eclient_runtime::modules::visual::NoHurtCam::update(player);
    }
    if (on("NoBlur")) {
        eclient_runtime::modules::visual::NoBlur::update(player);
    }
    if (on("NoCaveVignette")) {
        eclient_runtime::modules::visual::NoCaveVignette::update(player);
    }
    if (on("NoEmoteCooldown")) {
        eclient_runtime::modules::player::NoEmoteCooldown::update(player);
    }
    if (on("NoHurt") && player) {
        eclient_runtime::modules::misc::NoHurt::update(player);
    }
    if (on("NoFire") && player) {
        eclient_runtime::modules::player::NoFire::update(player);
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
    __android_log_print(ANDROID_LOG_INFO, TAG, "1.21.111 gameplay hooks installed: Step/Sprint + guarded feature toggles");
    return true;
}
void shutdown() {}
} // namespace eclient_runtime::modules::gameplay
