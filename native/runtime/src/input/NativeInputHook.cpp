#include "input/NativeInputHook.h"

#include <android/input.h>
#include <jni.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <dlfcn.h>
#include <atomic>
#include <string_view>

#include "dobby.h"
#include "gui/InGameGui.h"
#include "bridge/GameBridge.h"

namespace eclient_runtime::input {
namespace {
constexpr const char* TAG = "EClientInput";
using NativeKeyHandler = bool (*)(void*, void*, int, int);
using NativeTouchHandler = jint (*)(JNIEnv*, jobject, jint, jint, jfloat, jfloat);
using GetEventFn = int32_t (*)(AInputQueue*, AInputEvent**);
using RegisterNativesFn = jint (*)(JNIEnv*, jclass, const JNINativeMethod*, jint);

std::atomic_bool g_keyInstalled{false};
std::atomic_bool g_touchInstalled{false};
std::atomic_bool g_jniTouchInstalled{false};
std::atomic_bool g_registerNativesInstalled{false};
NativeKeyHandler g_original = nullptr;
NativeTouchHandler g_originalTouch = nullptr;
GetEventFn g_originalGetEvent = nullptr;
RegisterNativesFn g_originalRegisterNatives = nullptr;

bool hookedNativeKeyHandler(void* a1, void* a2, int keyCode, int keyAction) {
    // Volume-up toggles the in-game menu (ACTION_DOWN only).
    if (keyCode == AKEYCODE_VOLUME_UP && keyAction == 0) {
        eclient_runtime::host::InGameGui::toggleMenu();
    }
    return g_original ? g_original(a1, a2, keyCode, keyAction) : false;
}

bool routeTouchAction(int action, float x, float y) {
    const int masked = action & AMOTION_EVENT_ACTION_MASK;
    switch (masked) {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN:
            eclient_runtime::host::InGameGui::submitTouch(x, y, 0);
            return true;
        case AMOTION_EVENT_ACTION_MOVE:
            eclient_runtime::host::InGameGui::submitTouch(x, y, 1);
            return true;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:
        case AMOTION_EVENT_ACTION_CANCEL:
            eclient_runtime::host::InGameGui::submitTouch(x, y, 2);
            return true;
        default:
            return false;
    }
}

jint hookedNativeTouchHandler(JNIEnv* env, jobject activity, jint action, jint pointerId,
                              jfloat x, jfloat y) {
    if (!eclient_runtime::host::InGameGui::menuOpen() ||
        !routeTouchAction(action, x, y)) {
        return g_originalTouch
                   ? g_originalTouch(env, activity, action, pointerId, x, y)
                   : JNI_FALSE;
    }

    // Some Minecraft builds declare this callback as void while others return
    // a boolean/int handled flag. The return value is ignored for void JNI
    // methods, and true is the correct result for the handled variants.
    return JNI_TRUE;
}

bool isTouchRegistration(const char* name, const char* signature) {
    if (!name || !signature) return false;

    const std::string_view method{name};
    const bool namedTouchMethod =
        method.find("Touch") != std::string_view::npos ||
        method.find("touch") != std::string_view::npos ||
        method.find("Motion") != std::string_view::npos ||
        method.find("motion") != std::string_view::npos ||
        method.find("Mouse") != std::string_view::npos ||
        method.find("mouse") != std::string_view::npos;

    // Minecraft's Android input callback is normally (int, int, float, float).
    // Only hook this known ABI; matching by name alone would risk patching an
    // unrelated native method with incompatible arguments.
    const std::string_view sig{signature};
    const bool knownTouchAbi =
        sig == "(IIFF)V" || sig == "(IIFF)Z" || sig == "(IIFF)I";
    return namedTouchMethod && knownTouchAbi;
}

jint hookedRegisterNatives(JNIEnv* env, jclass clazz,
                           const JNINativeMethod* methods, jint count) {
    if (methods && count > 0 && !g_originalTouch) {
        for (jint i = 0; i < count; ++i) {
            const auto& method = methods[i];
            if (!isTouchRegistration(method.name, method.signature) || !method.fn) continue;

            if (DobbyHook(method.fn, reinterpret_cast<void*>(hookedNativeTouchHandler),
                          reinterpret_cast<void**>(&g_originalTouch)) == RS_SUCCESS &&
                g_originalTouch) {
                g_jniTouchInstalled.store(true);
                __android_log_print(ANDROID_LOG_INFO, TAG,
                                    "Hooked dynamically registered input method: %s %s",
                                    method.name, method.signature);
            } else {
                g_originalTouch = nullptr;
                __android_log_print(ANDROID_LOG_WARN, TAG,
                                    "Could not hook dynamically registered input method: %s %s",
                                    method.name, method.signature);
            }
            break;
        }
    }

    return g_originalRegisterNatives
               ? g_originalRegisterNatives(env, clazz, methods, count)
               : JNI_ERR;
}

// While the menu is open, motion events are routed to ImGui and consumed so the
// game camera does not move behind the menu.
int32_t hookedGetEvent(AInputQueue* queue, AInputEvent** outEvent) {
    const int32_t result = g_originalGetEvent ? g_originalGetEvent(queue, outEvent) : -1;
    if (result < 0 || !outEvent || !*outEvent) return result;
    if (!eclient_runtime::host::InGameGui::menuOpen()) return result;

    AInputEvent* ev = *outEvent;
    if (AInputEvent_getType(ev) != AINPUT_EVENT_TYPE_MOTION) return result;

    const int action = AMotionEvent_getAction(ev);
    const int masked = action & AMOTION_EVENT_ACTION_MASK;
    const int pointerIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                             >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

    // Prefer the active pointer for multi-touch; fall back to index 0.
    size_t idx = 0;
    if (masked == AMOTION_EVENT_ACTION_POINTER_DOWN ||
        masked == AMOTION_EVENT_ACTION_POINTER_UP) {
        idx = static_cast<size_t>(pointerIndex);
    }
    if (idx >= AMotionEvent_getPointerCount(ev)) idx = 0;

    const float x = AMotionEvent_getX(ev, idx);
    const float y = AMotionEvent_getY(ev, idx);

    // Leave hover/button events that are outside the touch adapter to
    // Minecraft. Only finish an event after it has been accepted by the GUI
    // route; otherwise a physical mouse or an unknown motion action would be
    // silently discarded.
    if (!routeTouchAction(action, x, y)) return result;

    // Consume so Minecraft does not also process the gesture.
    AInputQueue_finishEvent(queue, ev, 1);
    *outEvent = nullptr;
    return -1;
}

bool installJniRegistrationHookImpl(JavaVM* vm) {
    if (g_registerNativesInstalled.load()) return true;
    if (!vm) return false;

    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK || !env || !env->functions) {
        __android_log_print(ANDROID_LOG_WARN, TAG,
                            "Unable to access JNI table for RegisterNatives hook");
        return false;
    }

    auto registerNatives = env->functions->RegisterNatives;
    if (!registerNatives) return false;

    if (DobbyHook(reinterpret_cast<void*>(registerNatives),
                  reinterpret_cast<void*>(hookedRegisterNatives),
                  reinterpret_cast<void**>(&g_originalRegisterNatives)) != RS_SUCCESS ||
        !g_originalRegisterNatives) {
        g_originalRegisterNatives = nullptr;
        __android_log_print(ANDROID_LOG_WARN, TAG, "RegisterNatives hook failed");
        return false;
    }

    g_registerNativesInstalled.store(true);
    __android_log_print(ANDROID_LOG_INFO, TAG,
                        "JNI RegisterNatives hook ready for dynamic touch callbacks");
    return true;
}

void* findInMinecraft(const char* symbol) {
    const auto snap = eclient_runtime::GameBridge::instance().snapshot();
    const char* candidates[2] = {snap.libraryPath.empty() ? nullptr : snap.libraryPath.c_str(),
                                 "libminecraftpe.so"};
    for (const char* lib : candidates) {
        if (!lib) continue;
        void* handle = dlopen(lib, RTLD_NOW | RTLD_NOLOAD);
        if (!handle) continue;
        void* sym = dlsym(handle, symbol);
        dlclose(handle);
        if (sym) return sym;
    }
    return dlsym(RTLD_DEFAULT, symbol);
}

bool installKeyHook() {
    if (g_keyInstalled.load()) return true;

    static const char* candidates[] = {
        "Java_com_mojang_minecraftpe_MainActivity_nativeKeyHandler",
        "Java_com_mojang_minecraftpe_MainActivity_nativeKeyHandler__",
        "Java_com_mojang_minecraftpe_MainActivity_nativeKeyDown",
        "Java_com_mojang_minecraftpe_MainActivity_nativeKeyUp"
    };

    void* symbol = nullptr;
    for (const char* name : candidates) {
        symbol = findInMinecraft(name);
        if (symbol) {
            __android_log_print(ANDROID_LOG_INFO, TAG, "Found hotkey export: %s", name);
            break;
        }
    }

    if (!symbol) {
        __android_log_print(ANDROID_LOG_WARN, TAG, "nativeKeyHandler export not found; GUI will remain available via touch-only fallback");
        return false;
    }
    if (DobbyHook(symbol, reinterpret_cast<void*>(hookedNativeKeyHandler),
                  reinterpret_cast<void**>(&g_original)) != RS_SUCCESS || !g_original) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "nativeKeyHandler hook failed");
        g_original = nullptr;
        return false;
    }
    g_keyInstalled.store(true);
    __android_log_print(ANDROID_LOG_INFO, TAG, "Mobile GUI hotkey ready: Volume Up");
    return true;
}

bool installTouchHook() {
    if (g_touchInstalled.load()) return true;
    void* symbol = dlsym(RTLD_DEFAULT, "AInputQueue_getEvent");
    if (!symbol) symbol = reinterpret_cast<void*>(&AInputQueue_getEvent);
    if (!symbol) {
        __android_log_print(ANDROID_LOG_WARN, TAG,
                            "nativeTouchEvent export not found; relying on AInputQueue_getEvent");
        return false;
    }
    if (DobbyHook(symbol, reinterpret_cast<void*>(hookedGetEvent),
                  reinterpret_cast<void**>(&g_originalGetEvent)) != RS_SUCCESS ||
        !g_originalGetEvent) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "AInputQueue_getEvent hook failed");
        g_originalGetEvent = nullptr;
        return false;
    }
    g_touchInstalled.store(true);
    __android_log_print(ANDROID_LOG_INFO, TAG, "Touch routing for the GUI ready");
    return true;
}

bool installJniTouchHook() {
    if (g_jniTouchInstalled.load()) return true;

    static const char* candidates[] = {
        "Java_com_mojang_minecraftpe_MainActivity_nativeTouchEvent",
        "Java_com_mojang_minecraftpe_MainActivity_nativeTouchEvent__",
        "Java_com_mojang_minecraftpe_MainActivity_nativeTouchEvent__IIFF"
    };

    void* symbol = nullptr;
    for (const char* name : candidates) {
        symbol = findInMinecraft(name);
        if (symbol) {
            __android_log_print(ANDROID_LOG_INFO, TAG, "Found touch export: %s", name);
            break;
        }
    }
    if (!symbol) return false;

    if (DobbyHook(symbol, reinterpret_cast<void*>(hookedNativeTouchHandler),
                  reinterpret_cast<void**>(&g_originalTouch)) != RS_SUCCESS ||
        !g_originalTouch) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "nativeTouchEvent hook failed");
        g_originalTouch = nullptr;
        return false;
    }
    g_jniTouchInstalled.store(true);
    __android_log_print(ANDROID_LOG_INFO, TAG, "JNI touch routing for the GUI ready");
    return true;
}
} // namespace

bool installJniRegistrationHook(JavaVM* vm) {
    return installJniRegistrationHookImpl(vm);
}

bool initialize() {
    const bool key = installKeyHook();
    const bool queueTouch = installTouchHook();
    const bool jniTouch = installJniTouchHook();
    const bool touch = queueTouch || jniTouch;
    if (!key) {
        // No hotkey: open the menu so it remains reachable.
        eclient_runtime::host::InGameGui::toggleMenu();
    }
    if (!touch) {
        __android_log_print(ANDROID_LOG_WARN, TAG,
                            "Touch hook failed — GUI will show but may not receive taps");
    }
    return key || touch;
}

void shutdown() {
    g_keyInstalled.store(false);
    g_touchInstalled.store(false);
    g_jniTouchInstalled.store(false);
    g_registerNativesInstalled.store(false);
}

} // namespace eclient_runtime::input
