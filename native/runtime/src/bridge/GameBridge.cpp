#include "bridge/GameBridge.h"

#include <link.h>
#include <elf.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <android/log.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector>
#include "memory/NativeMemory.h"

namespace eclient_runtime {
namespace {
constexpr const char* TAG = "EClientRuntime";
constexpr const char* TARGET_LIBRARY = "libminecraftpe.so";
// Build ID of the exact 1.21.111 ARM64 library supplied for this project.
constexpr const char* EXPECTED_BUILD_ID = "ea5614dcc3551721b14f9b4686aa1651c1b8f79e"; // Minecraft 1.21.111 ARM64

struct ModuleInfo {
    uintptr_t base{};
    const char* path{};
    std::string buildId{};
};

std::string hexDigest(const unsigned char* data, std::size_t size) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < size; ++i) out << std::setw(2) << static_cast<unsigned>(data[i]);
    return out.str();
}

// Walks an ELF note segment and returns the GNU build-id as lowercase hex.
std::string parseBuildIdNotes(const unsigned char* notes, std::size_t size) {
    const unsigned char* cursor = notes;
    const unsigned char* end = notes + size;
    while (cursor + sizeof(Elf64_Nhdr) <= end) {
        Elf64_Nhdr note{};
        std::memcpy(&note, cursor, sizeof(note));
        cursor += sizeof(note);
        const std::size_t nameSize = (note.n_namesz + 3u) & ~3u;
        const std::size_t descSize = (note.n_descsz + 3u) & ~3u;
        if (cursor + nameSize + descSize > end) break;
        const char* noteName = reinterpret_cast<const char*>(cursor);
        const unsigned char* desc = cursor + nameSize;
        if (note.n_type == NT_GNU_BUILD_ID && note.n_namesz >= 3 &&
            std::memcmp(noteName, "GNU", 3) == 0) {
            return hexDigest(desc, note.n_descsz);
        }
        cursor += nameSize + descSize;
    }
    return {};
}

// Preferred: read the note straight out of the already-mapped library. This works even
// when libminecraftpe.so is loaded from inside the APK (dlpi_name is not a plain file).
std::string readBuildIdFromMemory(const struct dl_phdr_info* info) {
    if (!info || !info->dlpi_phdr) return {};
    for (Elf64_Half i = 0; i < info->dlpi_phnum; ++i) {
        const Elf64_Phdr& ph = info->dlpi_phdr[i];
        if (ph.p_type != PT_NOTE || ph.p_memsz == 0) continue;
        const auto* notes = reinterpret_cast<const unsigned char*>(
            static_cast<std::uintptr_t>(info->dlpi_addr) + static_cast<std::uintptr_t>(ph.p_vaddr));
        std::string id = parseBuildIdNotes(notes, static_cast<std::size_t>(ph.p_memsz));
        if (!id.empty()) return id;
    }
    return {};
}

// Fallback: read the note from the file on disk.
std::string readBuildId(const char* path) {
    if (!path || !*path) return {};
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return {};

    Elf64_Ehdr eh{};
    if (pread(fd, &eh, sizeof(eh), 0) != static_cast<ssize_t>(sizeof(eh)) ||
        std::memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0 ||
        eh.e_ident[EI_CLASS] != ELFCLASS64 || eh.e_phentsize != sizeof(Elf64_Phdr)) {
        close(fd);
        return {};
    }

    for (Elf64_Half i = 0; i < eh.e_phnum; ++i) {
        Elf64_Phdr ph{};
        const off_t off = static_cast<off_t>(eh.e_phoff) +
                          static_cast<off_t>(i) * static_cast<off_t>(eh.e_phentsize);
        if (pread(fd, &ph, sizeof(ph), off) != static_cast<ssize_t>(sizeof(ph))) break;
        if (ph.p_type != PT_NOTE || ph.p_filesz == 0) continue;

        std::vector<unsigned char> notes(static_cast<std::size_t>(ph.p_filesz));
        if (pread(fd, notes.data(), notes.size(), static_cast<off_t>(ph.p_offset)) !=
            static_cast<ssize_t>(notes.size())) continue;

        std::string id = parseBuildIdNotes(notes.data(), notes.size());
        if (!id.empty()) {
            close(fd);
            return id;
        }
    }
    close(fd);
    return {};
}

int findMinecraft(struct dl_phdr_info* info, size_t, void* opaque) {
    auto* result = static_cast<ModuleInfo*>(opaque);
    if (!info || !info->dlpi_name || !*info->dlpi_name) return 0;
    const char* slash = std::strrchr(info->dlpi_name, '/');
    const char* name = slash ? slash + 1 : info->dlpi_name;
    if (std::strcmp(name, TARGET_LIBRARY) != 0) return 0;

    result->base = static_cast<uintptr_t>(info->dlpi_addr);
    result->path = info->dlpi_name;
    result->buildId = readBuildIdFromMemory(info);
    if (result->buildId.empty()) result->buildId = readBuildId(info->dlpi_name);
    return 1;
}

} // namespace

GameBridge& GameBridge::instance() {
    static GameBridge bridge;
    return bridge;
}


bool GameBridge::discoverMinecraft() {
    ModuleInfo info{};
    dl_iterate_phdr(findMinecraft, &info);
    if (!info.base) {
        std::lock_guard lock(mutex_);
        snapshot_.libraryLoaded = false;
        snapshot_.fingerprintMatched = false;
        snapshot_.symbolsReady = false;
        snapshot_.status = "waiting for libminecraftpe.so";
        return false;
    }

    moduleBase_ = info.base;
    modulePath_ = info.path ? info.path : TARGET_LIBRARY;
    buildId_ = info.buildId;

    {
        std::lock_guard lock(mutex_);
        snapshot_.libraryLoaded = true;
        snapshot_.buildId = buildId_;
        snapshot_.libraryPath = modulePath_;
    snapshot_.moduleBase = moduleBase_;
    }
    return true;
}

bool GameBridge::verifyFingerprint() {
    const bool matched = buildId_ == EXPECTED_BUILD_ID;
    std::lock_guard lock(mutex_);
    snapshot_.fingerprintMatched = matched;
    // This bridge is only valid when the real Minecraft library is loaded in the current process.
    snapshot_.symbolsReady = matched;
    snapshot_.hookInstalled = false;
    snapshot_.status = matched
        ? "1.21.111 ARM64 target verified in-process"
        : "Minecraft library found, but build ID does not match 1.21.111.3 profile";
    return matched;
}

bool GameBridge::initialize() {
    if (running_.exchange(true)) return true;

    discoverMinecraft();
    verifyFingerprint();
    heartbeatThread_ = std::thread([this] { heartbeatLoop(); });

    initializeMemoryProfile();

    const auto s = snapshot();
    __android_log_print(ANDROID_LOG_INFO, TAG,
        "Native runtime: library=%s fingerprint=%s build=%s",
        s.libraryLoaded ? "yes" : "no",
        s.fingerprintMatched ? "match" : "no-match",
        s.buildId.empty() ? "none" : s.buildId.c_str());
    return true;
}


bool GameBridge::initializeMemoryProfile() {
    memory::ModuleInfo module{};
    const bool found = memory::NativeMemory::instance().locateModule(TARGET_LIBRARY, module);
    std::lock_guard lock(mutex_);
    if (!found) {
        snapshot_.symbolsReady = false;
        return false;
    }
    snapshot_.moduleBase = module.base;
    snapshot_.executableRangeCount = module.executableRanges.size();
    snapshot_.symbolsReady = snapshot_.fingerprintMatched && !module.executableRanges.empty();
    return snapshot_.symbolsReady;
}

void GameBridge::shutdown() {
    running_.store(false);
    if (heartbeatThread_.joinable()) heartbeatThread_.join();
    std::lock_guard lock(mutex_);
    snapshot_.hookInstalled = false;
    snapshot_.playerSeen = false;
    snapshot_.status = "native runtime stopped";
}

void GameBridge::heartbeatLoop() {
    while (running_.load()) {
        // This heartbeat proves that E-Client native code is executing in its own
        // Android process. It deliberately does not masquerade as a Minecraft tick.
        const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        {
            std::lock_guard lock(mutex_);
            ++snapshot_.heartbeat;
            snapshot_.lastUpdateMs = nowMs;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

BridgeSnapshot GameBridge::snapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
}

} // namespace eclient_runtime
