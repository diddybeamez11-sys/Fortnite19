#include "profile/PatchManager.h"
#include "profile/Offsets_1_21_111.h"
#include <android/log.h>
#include <cstring>
#include <utility>

namespace eclient_runtime::profile {
namespace {
constexpr const char* TAG = "EClientPatches";
const std::vector<std::uint8_t> RET = {0xC0,0x03,0x5F,0xD6};
}

PatchManager& PatchManager::instance() { static PatchManager p; return p; }

bool PatchManager::initialize() {
    if (ready_) return true;
    if (!memory::NativeMemory::instance().locateModule("libminecraftpe.so", minecraft_)) {
        status_ = "waiting for libminecraftpe.so";
        return false;
    }

    // These entry bytes were read from the user-supplied 1.21.111 ARM64 library.
    specs_ = {
        {"NoHurtCam", mc_1_21_111::NoHurtCam, {0xff,0xc3,0x00,0xd1}, RET},
        {"NoCamDistortion", mc_1_21_111::NoCamDistortion, {0xff,0xc3,0x00,0xd1}, RET},
        {"NoBoatRotation", mc_1_21_111::NoBoatRotation, {0xff,0xc3,0x00,0xd1}, RET},
        {"NoCamSleep", mc_1_21_111::NoCamSleep, {0xff,0xc3,0x00,0xd1}, RET},
        {"PlaceCamera", mc_1_21_111::PlaceCamera, {0xff,0xc3,0x00,0xd1}, RET},
        {"SlowDownTriggers", mc_1_21_111::SlowDownTriggers, {0xff,0xc3,0x00,0xd1}, RET},
        {"NoSlowDown", mc_1_21_111::NoSlowDown, {0xff,0xc3,0x00,0xd1}, RET},
        {"NoWaterDrown", mc_1_21_111::WaterDrown, {0xff,0xc3,0x00,0xd1}, RET},
        {"NoLavaDrown", mc_1_21_111::LavaDrown, {0xff,0xc3,0x00,0xd1}, RET},
        {"Noclip", mc_1_21_111::Noclip, {0xff,0xc3,0x00,0xd1}, RET},
        {"XrayCameraThird", mc_1_21_111::XrayCameraThird, {0xff,0xc3,0x00,0xd1}, RET},
        {"NoBlur", mc_1_21_111::NoBlur1, {0xff,0xc3,0x05,0xd1}, RET},
        {"NoBlur", mc_1_21_111::NoBlur2, {0xff,0x03,0x05,0xd1}, RET},
        {"NoCaveVignette", mc_1_21_111::VignetteRenderer, {0xff,0x43,0x02,0xd1}, RET},
        {"AntiKnockback", mc_1_21_111::LerpMotion, {0x08,0x04,0x41,0xf9}, RET},
        {"AutoClickMine", mc_1_21_111::AutoClickMine, {0xfd,0x7b,0xbe,0xa9}, RET},
        {"NoEmoteCooldown", mc_1_21_111::IsEmoting, {0x81,0x0b,0x80,0x52,0xed,0xcd,0xec,0x17}, {0x00,0x00,0x80,0xd2,0xc0,0x03,0x5f,0xd6}},
        // FullBright is a float-returning function. The patch returns 999.0f.
        {"FullBright", mc_1_21_111::FullBright,
            {0xfd,0x7b,0xbf,0xa9,0xfd,0x03,0x00,0x91,0x08,0x00,0x40,0xf9},
            {0x88,0x56,0xa8,0x52,0x00,0x01,0x27,0x1e,0xc0,0x03,0x5f,0xd6}},
    };
    active_.assign(specs_.size(), false);

    // Fail closed if any reference doesn't match the actual loaded image.
    for (const auto& s : specs_) {
        const auto address = minecraft_.base + s.offset;
        std::vector<std::uint8_t> actual(s.expected.size());
        if (!memory::NativeMemory::instance().read(address, actual.data(), actual.size()) || actual != s.expected) {
            __android_log_print(ANDROID_LOG_ERROR, TAG, "profile mismatch at %s + 0x%llX", s.module,
                static_cast<unsigned long long>(s.offset));
            status_ = "1.21.111 profile validation failed; no patches enabled";
            return false;
        }
    }
    ready_ = true;
    status_ = "1.21.111 ARM64 patch profile validated";
    return true;
}

bool PatchManager::set(const std::string& module, bool on) {
    if (!ready_ && !initialize()) return false;
    std::vector<std::size_t> matches;
    for (std::size_t i=0; i<specs_.size(); ++i) {
        if (module == specs_[i].module) matches.push_back(i);
    }
    if (matches.empty()) return false;

    // A UI module may cover more than one patch site (for example NoBlur).
    // Write every site, and undo the sites already changed if a later write
    // fails.  This prevents a checked module from representing a half-applied
    // feature.
    std::vector<std::pair<std::size_t, bool>> changed;
    for (const auto i : matches) {
        const auto address = minecraft_.base + specs_[i].offset;
        const auto& bytes = on ? specs_[i].enabled : specs_[i].expected;
        if (!memory::NativeMemory::instance().write(address, bytes.data(), bytes.size())) {
            for (const auto& [rollback, wasEnabled] : changed) {
                const auto rollbackAddress = minecraft_.base + specs_[rollback].offset;
                const auto& rollbackBytes = wasEnabled
                                                ? specs_[rollback].enabled
                                                : specs_[rollback].expected;
                (void)memory::NativeMemory::instance().write(
                    rollbackAddress, rollbackBytes.data(), rollbackBytes.size());
                active_[rollback] = wasEnabled;
            }
            return false;
        }
        changed.emplace_back(i, active_[i]);
        active_[i] = on;
    }
    return true;
}

bool PatchManager::enabled(const std::string& module) const {
    bool found = false;
    for (std::size_t i=0; i<specs_.size(); ++i) {
        if (module != specs_[i].module) continue;
        found = true;
        if (!active_[i]) return false;
    }
    return found;
}
bool PatchManager::ready() const { return ready_; }
std::string PatchManager::status() const { return status_; }
}
