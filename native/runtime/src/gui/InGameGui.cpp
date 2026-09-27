#include "gui/InGameGui.h"

#include <EGL/egl.h>
#include <android/keycodes.h>
#include <android/native_window.h>
#include <GLES2/gl2.h>
#include <android/log.h>
#include <dlfcn.h>
#include <cfloat>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <vector>
#include <string>
#include <unordered_set>
#include <unordered_map>

#include "imgui.h"
#include "backends/imgui_impl_android.h"
#include "backends/imgui_impl_opengl3.h"
#include "dobby.h"
#include "bridge/GameBridge.h"
#include "modules/ModuleManager.h"

namespace eclient_runtime::host::InGameGui {
namespace {
constexpr const char* TAG = "EClientGui";
std::mutex g_mutex;
std::atomic_bool g_ready{false};
std::atomic_bool g_imguiReady{false};
std::atomic_bool g_menuOpen{false};

struct TouchSample {
    float x{0};
    float y{0};
    int action{0};
};
std::mutex g_touchMutex;
std::vector<TouchSample> g_touchQueue;
float g_lastTouchX = -1.0f;
float g_lastTouchY = -1.0f;
bool g_lastTouchDown = false;
char g_search[64] = {};
bool g_enabledOnly = false;
struct KeySample {
    int keyCode;
    bool down;
};
std::mutex g_keyMutex;
std::vector<KeySample> g_keyQueue;
std::unordered_set<int> g_pressedKeys;
std::unordered_map<std::string, int> g_moduleKeybinds;

struct KeyChoice {
    int code;
    std::string name;
};

const std::vector<KeyChoice>& keyChoices() {
    static const std::vector<KeyChoice> choices = [] {
        std::vector<KeyChoice> result{{0, "None"}};
        static constexpr char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        for (int i = 0; i < 26; ++i) result.push_back({AKEYCODE_A + i, std::string(1, letters[i])});
        for (int i = 0; i < 10; ++i) result.push_back({AKEYCODE_0 + i, std::string(1, "0123456789"[i])});
        for (int i = 0; i < 12; ++i) result.push_back({AKEYCODE_F1 + i, "F" + std::to_string(i + 1)});
        result.push_back({AKEYCODE_SPACE, "Space"});
        result.push_back({AKEYCODE_ENTER, "Enter"});
        result.push_back({AKEYCODE_DPAD_UP, "Up"});
        result.push_back({AKEYCODE_DPAD_DOWN, "Down"});
        result.push_back({AKEYCODE_DPAD_LEFT, "Left"});
        result.push_back({AKEYCODE_DPAD_RIGHT, "Right"});
        return result;
    }();
    return choices;
}

void assignKeybind(const std::string& moduleName, int keyCode) {
    for (auto& [name, boundKey] : g_moduleKeybinds) {
        if (name != moduleName && boundKey == keyCode) boundKey = 0;
    }
    g_moduleKeybinds[moduleName] = keyCode;
}

void processKeyEvents() {
    std::vector<KeySample> pending;
    {
        std::lock_guard lock(g_keyMutex);
        pending.swap(g_keyQueue);
    }

    auto& manager = eclient_runtime::modules::ModuleManager::instance();
    for (const auto& event : pending) {
        if (!event.down) {
            g_pressedKeys.erase(event.keyCode);
            continue;
        }
        if (!g_pressedKeys.insert(event.keyCode).second) continue;
        for (const auto& [moduleName, keyCode] : g_moduleKeybinds) {
            if (keyCode == event.keyCode) {
                manager.setEnabled(moduleName, !manager.enabled(moduleName));
                break;
            }
        }
    }
}

using SwapBuffersFn = EGLBoolean (*)(EGLDisplay, EGLSurface);
SwapBuffersFn g_originalSwap = nullptr;
std::atomic<int> g_physicalWidth{0};
std::atomic<int> g_physicalHeight{0};

void drawModuleList() {
    auto& mgr = eclient_runtime::modules::ModuleManager::instance();
    const auto& modules = mgr.all();

    static constexpr const char* kOrder[] = {"Combat", "Movement", "Visual", "Player", "Misc"};
    std::unordered_map<std::string, std::vector<const eclient_runtime::modules::ModuleState*>> byCat;

    const std::string filter = g_search;
    bool anyVisible = false;
    std::size_t enabledCount = 0;
    for (const auto& m : modules) {
        if (m.enabled) ++enabledCount;
        if (g_enabledOnly && !m.enabled) continue;
        const std::string cat = m.category.empty() ? "Misc" : m.category;
        if (!filter.empty()) {
            const std::string needle = filter;
            const std::string hay = m.name + " " + m.description + " " + m.category;
            const auto pos = hay.find(needle);
            if (pos == std::string::npos) {
                continue;
            }
        }
        byCat[cat].push_back(&m);
        anyVisible = true;
    }

    ImGui::Text("MODULES");
    ImGui::SameLine();
    ImGui::TextDisabled("%zu enabled", enabledCount);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##module_search", "Search modules...", g_search, sizeof(g_search));
    ImGui::Checkbox("Enabled only", &g_enabledOnly);

    if (!anyVisible) {
        ImGui::TextDisabled("No modules match the current filter.");
        return;
    }

    if (ImGui::BeginTabBar("##cats", ImGuiTabBarFlags_FittingPolicyScroll)) {
        auto drawRows = [](const char* id, const auto& rows) {
            ImGui::BeginChild(id, ImVec2(0.0f, 0.0f), false,
                              ImGuiWindowFlags_AlwaysVerticalScrollbar);
            for (const auto* module : rows) {
                bool enabled = module->enabled;
                ImGui::PushID(module->name.c_str());
                const bool toggled = ImGui::Checkbox(module->name.c_str(), &enabled);
                if (toggled && !eclient_runtime::modules::ModuleManager::instance().setEnabled(
                                   module->name, enabled)) {
                    enabled = module->enabled;
                }
                int& keyCode = g_moduleKeybinds[module->name];
                const char* keyName = "None";
                for (const auto& choice : keyChoices()) {
                    if (choice.code == keyCode) {
                        keyName = choice.name.c_str();
                        break;
                    }
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(118.0f);
                if (ImGui::BeginCombo("##keybind", keyName)) {
                    for (const auto& choice : keyChoices()) {
                        const bool selected = keyCode == choice.code;
                        if (ImGui::Selectable(choice.name.c_str(), selected)) {
                            assignKeybind(module->name, choice.code);
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s\n[%s]%s",
                                      module->description.c_str(),
                                      module->category.c_str(),
                                      module->memoryBacked ? " memory patch" : " runtime hook");
                }
                if (!module->description.empty()) {
                    ImGui::TextWrapped("%s", module->description.c_str());
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        };

        std::vector<const eclient_runtime::modules::ModuleState*> allRows;
        for (const char* cat : kOrder) {
            const auto it = byCat.find(cat);
            if (it != byCat.end()) allRows.insert(allRows.end(), it->second.begin(), it->second.end());
        }
        char allLabel[32];
        std::snprintf(allLabel, sizeof(allLabel), "All  %zu###tab_all", allRows.size());
        if (ImGui::BeginTabItem(allLabel)) {
            drawRows("##scroll_all", allRows);
            ImGui::EndTabItem();
        }

        for (const char* cat : kOrder) {
            auto it = byCat.find(cat);
            if (it == byCat.end() || it->second.empty()) continue;
            char tabLabel[48];
            std::snprintf(tabLabel, sizeof(tabLabel), "%s  %zu###tab_%s", cat,
                          it->second.size(), cat);
            if (!ImGui::BeginTabItem(tabLabel)) continue;
            drawRows((std::string("##scroll_") + cat).c_str(), it->second);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

EGLBoolean hookedSwap(EGLDisplay display, EGLSurface surface) {
    if (g_ready.load()) {
        EGLint width = 0;
        EGLint height = 0;
        eglQuerySurface(display, surface, EGL_WIDTH, &width);
        eglQuerySurface(display, surface, EGL_HEIGHT, &height);
        const float uiScale = std::clamp(static_cast<float>(std::min(width, height)) / 720.0f,
                         0.85f, 1.65f);
        const int physW = g_physicalWidth.load();
        const int physH = g_physicalHeight.load();
        const bool needsScale = physW > 0 && physH > 0 && (physW != width || physH != height);
        const float scaleX = needsScale ? static_cast<float>(width) / static_cast<float>(physW) : 1.0f;
        const float scaleY = needsScale ? static_cast<float>(height) / static_cast<float>(physH) : 1.0f;

        if (!g_imguiReady.exchange(true)) {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO& io0 = ImGui::GetIO();
            io0.IniFilename = nullptr;
            io0.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io0.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
            io0.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
            ImGui::StyleColorsDark();
            ImGuiStyle& style = ImGui::GetStyle();
            style.WindowPadding = ImVec2(18.0f * uiScale, 16.0f * uiScale);
            style.FramePadding = ImVec2(12.0f * uiScale, 10.0f * uiScale);
            style.ItemSpacing = ImVec2(12.0f * uiScale, 10.0f * uiScale);
            style.ItemInnerSpacing = ImVec2(9.0f * uiScale, 7.0f * uiScale);
            style.TouchExtraPadding = ImVec2(6.0f * uiScale, 6.0f * uiScale);
            style.ScrollbarSize = 24.0f * uiScale;
            style.WindowRounding = 12.0f * uiScale;
            style.FrameRounding = 8.0f * uiScale;
            style.GrabRounding = 8.0f * uiScale;
            style.TabRounding = 8.0f * uiScale;
            style.ChildRounding = 8.0f * uiScale;
            style.WindowBorderSize = 1.0f;
            style.FrameBorderSize = 0.0f;
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.071f, 0.075f, 0.97f);
            style.Colors[ImGuiCol_Border] = ImVec4(0.18f, 0.25f, 0.25f, 1.0f);
            style.Colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.14f, 0.15f, 1.0f);
            style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.14f, 0.22f, 0.21f, 1.0f);
            style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.15f, 0.28f, 0.24f, 1.0f);
            style.Colors[ImGuiCol_CheckMark] = ImVec4(0.35f, 0.91f, 0.68f, 1.0f);
            style.Colors[ImGuiCol_Button] = ImVec4(0.12f, 0.19f, 0.18f, 1.0f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.18f, 0.32f, 0.27f, 1.0f);
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.22f, 0.43f, 0.33f, 1.0f);
            style.Colors[ImGuiCol_Header] = ImVec4(0.12f, 0.22f, 0.19f, 1.0f);
            style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.17f, 0.32f, 0.27f, 1.0f);
            style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.40f, 0.31f, 1.0f);
            style.Colors[ImGuiCol_Tab] = ImVec4(0.09f, 0.13f, 0.14f, 1.0f);
            style.Colors[ImGuiCol_TabHovered] = ImVec4(0.18f, 0.32f, 0.27f, 1.0f);
            style.Colors[ImGuiCol_TabActive] = ImVec4(0.13f, 0.25f, 0.21f, 1.0f);
            ImGui_ImplOpenGL3_Init("#version 300 es");
            __android_log_print(ANDROID_LOG_INFO, TAG, "ImGui renderer initialized");
        }

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
        io.FontGlobalScale = uiScale;

        const bool open = g_menuOpen.load();

        // Process touch input - always drain queue
        {
            std::lock_guard lock(g_touchMutex);
            if (!g_touchQueue.empty()) {
                for (const auto& sample : g_touchQueue) {
                    const float sx = sample.x * scaleX;
                    const float sy = sample.y * scaleY;
                    g_lastTouchX = sx;
                    g_lastTouchY = sy;
                    io.AddMousePosEvent(sx, sy);
                    if (sample.action == 0) {
                        g_lastTouchDown = true;
                        io.AddMouseButtonEvent(0, true);
                    } else if (sample.action == 2) {
                        g_lastTouchDown = false;
                        io.AddMouseButtonEvent(0, false);
                    }
                }
                g_touchQueue.clear();
            } else if (open) {
                // Menu open: keep sending position to ImGui
                if (g_lastTouchDown) {
                    io.AddMousePosEvent(g_lastTouchX, g_lastTouchY);
                    io.AddMouseButtonEvent(0, true);
                } else if (g_lastTouchX >= 0.0f) {
                    io.AddMousePosEvent(g_lastTouchX, g_lastTouchY);
                    io.AddMouseButtonEvent(0, false);
                }
            } else {
                // Menu closed: tell ImGui no input
                io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                io.AddMouseButtonEvent(0, false);
                g_lastTouchX = -1.0f;
                g_lastTouchY = -1.0f;
                g_lastTouchDown = false;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        processKeyEvents();

        if (open) {
            const float winW = std::min(760.0f * uiScale, static_cast<float>(width) * 0.94f);
            const float winH = std::min(860.0f * uiScale, static_cast<float>(height) * 0.90f);
            ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowPos(ImVec2((static_cast<float>(width) - winW) * 0.5f,
                                           (static_cast<float>(height) - winH) * 0.5f),
                                    ImGuiCond_FirstUseEver);

            bool keepOpen = open;
            const auto bridge = eclient_runtime::GameBridge::instance().snapshot();
            ImGui::Begin("##main", &keepOpen,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize);
            const float closeWidth = ImGui::GetFrameHeight();
            const float dragWidth = std::max(0.0f, ImGui::GetContentRegionAvail().x - closeWidth -
                                                      ImGui::GetStyle().ItemSpacing.x);
            const ImVec2 headerPos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##drag_panel", ImVec2(dragWidth, ImGui::GetFrameHeight()));
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 newPos = ImGui::GetWindowPos();
                newPos.x += io.MouseDelta.x;
                newPos.y += io.MouseDelta.y;
                ImGui::SetWindowPos(newPos);
            }
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 titleSize = ImGui::CalcTextSize("E / CLIENT");
            drawList->AddText(headerPos, ImGui::GetColorU32(ImGuiCol_CheckMark), "E / CLIENT");
            drawList->AddText(ImVec2(headerPos.x + titleSize.x + 12.0f * uiScale, headerPos.y),
                              ImGui::GetColorU32(ImGuiCol_TextDisabled), "1.21.111");
            ImGui::SameLine();
            if (ImGui::Button("X")) keepOpen = false;
            const bool runtimeReady = bridge.libraryLoaded && bridge.fingerprintMatched;
            ImGui::TextColored(runtimeReady ? ImVec4(0.35f, 0.91f, 0.68f, 1.0f)
                                            : ImVec4(0.96f, 0.66f, 0.30f, 1.0f),
                               "%s", runtimeReady ? "RUNTIME READY" : "WAITING FOR GAME");
            ImGui::SameLine();
            ImGui::TextDisabled("%s", bridge.status.c_str());
            ImGui::Separator();
            drawModuleList();
            ImGui::End();
            if (!keepOpen && g_menuOpen.load()) toggleMenu();
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
    return g_originalSwap ? g_originalSwap(display, surface) : EGL_FALSE;
}

using CreateWindowSurfaceFn = EGLSurface (*)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*);
CreateWindowSurfaceFn g_originalCreateSurface = nullptr;

EGLSurface hookedCreateWindowSurface(EGLDisplay display, EGLConfig config,
                                      EGLNativeWindowType window, const EGLint* attribs) {
    if (window) {
        auto* nativeWindow = reinterpret_cast<ANativeWindow*>(window);
        g_physicalWidth.store(ANativeWindow_getWidth(nativeWindow));
        g_physicalHeight.store(ANativeWindow_getHeight(nativeWindow));
    }
    return g_originalCreateSurface ? g_originalCreateSurface(display, config, window, attribs)
                                   : EGL_NO_SURFACE;
}

bool hookSwapBuffers() {
    if (g_originalSwap) return true;
    void* swapSymbol = dlsym(RTLD_DEFAULT, "eglSwapBuffers");
    if (!swapSymbol) swapSymbol = reinterpret_cast<void*>(eglSwapBuffers);
    if (!swapSymbol) return false;
    if (DobbyHook(swapSymbol, reinterpret_cast<void*>(hookedSwap),
                  reinterpret_cast<void**>(&g_originalSwap)) != RS_SUCCESS) {
        return false;
    }

    void* createSymbol = dlsym(RTLD_DEFAULT, "eglCreateWindowSurface");
    if (!createSymbol) createSymbol = reinterpret_cast<void*>(eglCreateWindowSurface);
    if (createSymbol) {
        DobbyHook(createSymbol, reinterpret_cast<void*>(hookedCreateWindowSurface),
                  reinterpret_cast<void**>(&g_originalCreateSurface));
    }
    return g_originalSwap != nullptr;
}
} // namespace

void toggleMenu() {
    bool wasOpen = g_menuOpen.load();
    while (!g_menuOpen.compare_exchange_weak(wasOpen, !wasOpen)) {
    }

    if (wasOpen) {
        std::lock_guard lock(g_touchMutex);
        g_touchQueue.clear();
        g_lastTouchX = -1.0f;
        g_lastTouchY = -1.0f;
        g_lastTouchDown = false;
    }
}

void setPhysicalWindowSize(int width, int height) {
    g_physicalWidth.store(width);
    g_physicalHeight.store(height);
}

void submitTouch(float x, float y, int action) {
    if (action < 0 || action > 2) return;
    std::lock_guard lock(g_touchMutex);
    if (g_touchQueue.size() > 64) g_touchQueue.erase(g_touchQueue.begin(), g_touchQueue.begin() + 32);
    g_touchQueue.push_back(TouchSample{x, y, action});
}

bool menuOpen() { return g_menuOpen.load(); }

void submitKeyEvent(int keyCode, int keyAction) {
    if (keyCode == AKEYCODE_VOLUME_UP || (keyAction != 0 && keyAction != 1)) return;
    std::lock_guard lock(g_keyMutex);
    if (g_keyQueue.size() >= 64) g_keyQueue.erase(g_keyQueue.begin(), g_keyQueue.begin() + 32);
    g_keyQueue.push_back(KeySample{keyCode, keyAction == 0});
}

bool initialize() {
    std::lock_guard lock(g_mutex);
    if (g_ready.load()) return true;
    if (!hookSwapBuffers()) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "eglSwapBuffers hook failed");
        return false;
    }
    g_ready.store(true);
    g_menuOpen.store(false);
    __android_log_print(ANDROID_LOG_INFO, TAG, "Render host ready");
    return true;
}

void shutdown() {
    std::lock_guard lock(g_mutex);
    g_ready.store(false);
    g_menuOpen.store(false);
    {
        std::lock_guard tlock(g_touchMutex);
        g_touchQueue.clear();
    }
    g_lastTouchX = -1.0f;
    g_lastTouchY = -1.0f;
    g_lastTouchDown = false;
    if (g_imguiReady.exchange(false)) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();
    }
}

} // namespace eclient_runtime::host::InGameGui
