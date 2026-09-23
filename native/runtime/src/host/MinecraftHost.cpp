#include "host/MinecraftHost.h"

#include <android/log.h>
#include <dlfcn.h>
#include <link.h>
#include <EGL/egl.h>
#include <chrono>
#include <cstring>

#include "gui/InGameGui.h"
#include "../bridge/GameBridge.h"
#include "../input/NativeInputHook.h"
#include "../profile/PatchManager.h"
#include "../modules/GameplayModules.h"

namespace eclient_runtime::host {
namespace {
constexpr const char* TAG = "EClientHost";
constexpr const char* TARGET = "libminecraftpe.so";
// This profile is deliberately a gate, not an offset table. Replace it when
// the corresponding Minecraft binary is supplied and verified.
constexpr const char* TARGET_BUILD_ID = "ea5614dcc3551721b14f9b4686aa1651c1b8f79e"; // 1.21.111 ARM64

struct FindResult {
    std::uintptr_t base{};
    const char* path{};
};

int findMinecraft(struct dl_phdr_info* info, size_t, void* opaque) {
    auto* result = static_cast<FindResult*>(opaque);
    if (!info || !info->dlpi_name || !*info->dlpi_name) return 0;
    const char* slash = std::strrchr(info->dlpi_name, '/');
    const char* name = slash ? slash + 1 : info->dlpi_name;
    if (std::strcmp(name, TARGET) != 0) return 0;
    result->base = static_cast<std::uintptr_t>(info->dlpi_addr);
    result->path = info->dlpi_name;
    return 1;
}

} // namespace

MinecraftHost& MinecraftHost::instance() {
    static MinecraftHost host;
    return host;
}

void MinecraftHost::start() {
    if (running_.exchange(true)) return;
    {
        std::lock_guard lock(mutex_);
        snapshot_.started = true;
        snapshot_.status = "waiting for Minecraft runtime";
    }
    thread_ = std::thread(&MinecraftHost::bootstrapLoop, this);
}

void MinecraftHost::stop() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
    InGameGui::shutdown();
    std::lock_guard lock(mutex_);
    snapshot_.renderHookReady = false;
    snapshot_.guiReady = false;
    snapshot_.status = "stopped";
}

HostSnapshot MinecraftHost::snapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
}

bool MinecraftHost::discoverMinecraft() {
    FindResult result{};
    dl_iterate_phdr(findMinecraft, &result);
    if (!result.base) return false;
    std::lock_guard lock(mutex_);
    snapshot_.minecraftLoaded = true;
    snapshot_.minecraftBase = result.base;
    snapshot_.minecraftPath = result.path ? result.path : TARGET;
    return true;
}

bool MinecraftHost::verifyTarget() {
    // Build-ID validation is performed by GameBridge. This host only consumes
    // the result and never invents compatibility for an unknown binary.
    const auto bridge = GameBridge::instance().snapshot();
    const bool matched = bridge.buildId == TARGET_BUILD_ID;
    std::lock_guard lock(mutex_);
    snapshot_.buildId = bridge.buildId;
    snapshot_.versionMatched = matched && bridge.libraryLoaded && bridge.symbolsReady;
    if (!snapshot_.versionMatched) snapshot_.status = "Minecraft loaded, target profile not verified";
    return snapshot_.versionMatched;
}

bool MinecraftHost::installRenderHook() {
    // GUI initialization is intentionally separated from game-memory hooks.
    // The first host milestone is proving that E-Client can coexist with the
    // real Minecraft process without pretending unknown offsets are valid.
    if (!InGameGui::initialize()) return false;
    // Input hooks are best-effort. A missing hotkey/touch export must not tear down the
    // render hook (re-hooking eglSwapBuffers on retry would chain the detour into itself).
    if (!eclient_runtime::input::initialize()) {
        __android_log_print(ANDROID_LOG_WARN, TAG,
            "No input hook available; GUI will render but cannot be toggled/touched");
    }
    std::lock_guard lock(mutex_);
    snapshot_.guiReady = true;
    snapshot_.renderHookReady = true;
    snapshot_.status = "E-Client host initialized inside Minecraft process";
    return true;
}

void MinecraftHost::updateStatus(const char* status) {
    std::lock_guard lock(mutex_);
    snapshot_.status = status ? status : "unknown";
}

void MinecraftHost::bootstrapLoop() {
    __android_log_print(ANDROID_LOG_INFO, TAG, "Host bootstrap started");
    while (running_.load()) {
        if (!discoverMinecraft()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }

        GameBridge::instance().initialize();
        if (!verifyTarget()) {
            // Unknown binaries are never patched. This makes version drift fail
            // closed instead of turning stale offsets into a crash.
            updateStatus("Minecraft runtime found; waiting for verified 1.21.111 ARM64 profile");
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        if (!eclient_runtime::profile::PatchManager::instance().initialize()) {
            updateStatus("1.21.111 runtime found, but patch profile validation failed");
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }
        if (!eclient_runtime::modules::gameplay::initialize()) {
            updateStatus("1.21.111 patch profile ready, gameplay hook installation failed");
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        if (installRenderHook()) {
            __android_log_print(ANDROID_LOG_INFO, TAG, "E-Client 1.21.111 in-game host ready");
            return;
        }
        updateStatus("GUI initialization failed");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

} // namespace eclient_runtime::host
