# E-Client Patch Runtime — Minecraft Bedrock 1.21.111 ARM64

A high-performance runtime patch injector and in-game GUI framework for Minecraft Bedrock Edition on Android. This repository contains the native runtime engine that instruments the game's rendering pipeline and input system to enable live module toggling without requiring a restart.

## Features

### 🎮 In-Game GUI System
- **ImGui-based overlay** with tabbed module browser
- **Touch-optimized interface** for mobile devices
- **Live module toggling** without game restart
- **Searchable module list** with descriptions and categories
- **Keybind customization** per module
- **Draggable window** — move the GUI anywhere on screen
- **Runtime feedback** — see modules enable/disable in real-time

### ⚙️ Module System
Supports runtime hooks and memory patches across these categories:

**Visual Enhancements:**
- NoHurtCam, FullBright, NoBlur, NoCaveVignette, NoWaterDrown, NoLavaDrown, NoCamDistortion, NoEmoteCooldown, NoBoatRotation, NoCamSleep, PlaceCamera, SlowDownTrigger

**Movement & Gameplay:**
- NoSlowDown, Noclip, AntiKnockback

*(Speed, Fly, KillAura, CriticalHit, ESP are available but disabled pending v1.21.111 verification)*

### 🔧 Input System
- **Dual-path touch routing**: JNI hook + AInputQueue interception
- **Input consumption**: Prevents game interaction when GUI is active
- **Hotkey support**: Configurable key bindings for module toggles
- **Automatic GUI fallback**: Opens menu if no hotkey is detected

### 🛡️ Security & Integrity
- **SHA-256 APK verification** before patching
- **GNU build ID validation** of libminecraftpe.so
- **Patch site integrity checking** per module
- **Memory fingerprint matching** to prevent misaligned patches

---

## Requirements

- **Android NDK** (tested with r25c and later)
- **Minecraft Bedrock 1.21.111 APK** (supplied by user)
- **ARM64 device** (no 32-bit support)
- Linux/macOS build environment

---

## Quick Start

### 1. Prepare Your APK

Download Minecraft Bedrock 1.21.111 for Android ARM64. The build scripts will verify the APK before patching.

### 2. Build & Patch

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk

./tools/build-and-patch.sh \
  /path/to/minecraft-1.21.111.apk \
  ./eclient-minecraft-1.21.111-arm64.apk
```

The script will:
- ✅ Compile the native ARM64 runtime with the NDK
- ✅ Validate the APK SHA-256
- ✅ Inject the runtime library
- ✅ Resign the APK with a debug keystore
- ✅ Output the patched APK

### 3. Install & Run

```bash
adb install -r eclient-minecraft-1.21.111-arm64.apk
```

Launch Minecraft on your device. The GUI should appear automatically or press your configured hotkey to open it.

---

## Usage

### Opening the GUI

**Option A:** Press the configured hotkey (if set)  
**Option B:** The GUI opens automatically if no hotkey is installed

### Toggling Modules

1. Tap the checkbox next to any module name to enable/disable it
2. The module takes effect immediately in-game
3. Changes persist across menu open/close cycles

### Customizing Keybinds

1. Open the GUI and find your desired module
2. Tap the keybind dropdown (currently showing "None")
3. Select a key from the list (A–Z, 0–9, F1–F12, arrow keys, etc.)
4. Only one module can be assigned to each key

### Searching Modules

1. Type in the "Search modules..." field to filter by:
   - Module name
   - Description
   - Category
2. Use "Enabled only" toggle to show only active modules

### Moving the GUI

- Drag the title bar ("E / CLIENT") to reposition the window
- Position is saved between sessions

---

## Architecture

### Rendering Pipeline Hook
- **eglSwapBuffers**: Intercepts each frame to render ImGui overlay
- **eglCreateWindowSurface**: Captures physical display dimensions for touch scaling
- **ImGui_ImplOpenGL3**: Renders the GUI using the game's OpenGL ES 3.0 context

### Input Pipeline
Two parallel input hooks ensure all touch events are captured:

1. **JNI Touch Hook**: Intercepts `nativeTouchEvent()` callback from Java
2. **AInputQueue Hook**: Intercepts `AInputQueue_getEvent()` at the NDK level

When the menu is open, both hooks route touches to ImGui and consume the events so the game camera doesn't move behind the menu.

### Module System
Each module is a runtime hook or memory patch that:
- Can be toggled on/off without restarting the game
- Hooks function entry points or patches game logic
- Reports its category, description, and current state
- Supports optional keybindings for instant toggle

---

## Building Without Patching

To build just the native runtime library:

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk
./tools/build-and-patch.sh --build-only
```

The compiled `libeclient.so` is placed in `native/build/arm64-v8a/`.

---

## File Structure

```
.
├── README.md                    # This file
├── tools/
│   ├── build-and-patch.sh      # Main build & patcher script
│   ├── patch-minecraft-apk.sh  # APK injection & signing
│   └── README.md               # Tool documentation
├── native/
│   ├── runtime/
│   │   └── src/
│   │       ├── gui/            # ImGui overlay (InGameGui.cpp/h)
│   │       ├── input/          # Touch & key routing (NativeInputHook.cpp/h)
│   │       ├── modules/        # Module implementations
│   │       ├── bridge/         # Game state access (GameBridge.cpp/h)
│   │       └── main.cpp        # Runtime initialization
│   ├── CMakeLists.txt          # NDK build configuration
│   └── build/                  # Compiled output (created after build)
└── ...
```

---

## Troubleshooting

### "Touch hook failed" in logcat

The GUI appears but doesn't respond to touches. This usually means both JNI and AInputQueue hooks failed.

**Fix:**
- Ensure you're running Minecraft 1.21.111 specifically
- Check that `libminecraftpe.so` is not stripped (symbols required for hooking)
- Try a clean rebuild: `rm -rf native/build && ./tools/build-and-patch.sh ...`

### GUI is visible but input goes to the game

The input routing may not have installed correctly. Check logcat:

```bash
adb logcat | grep -E "EClientInput|EClientGui"
```

Look for `"Touch routing for the GUI ready"` message. If absent, the hook failed.

### Module toggle has no effect

1. Verify the module is actually enabled (checkbox is ✓)
2. Check logcat for errors: `adb logcat | grep EClient`
3. Some modules may be disabled pending verification (see Features)
4. Try toggling again after a short delay

### APK verification failed

The SHA-256 of your APK doesn't match the expected Minecraft 1.21.111. Ensure:
- You downloaded the correct version from the Play Store
- The APK is not already modified
- You're using the exact file provided, not a repacked version

---

## Performance

- **GUI overhead**: <5% on mid-range Android devices (Snapdragon 855+)
- **Module overhead**: Varies by module; most <1% when disabled, <3% when active
- **Touch latency**: ~16–33ms (one frame at 30–60fps)
- **Memory footprint**: ~15–25 MB for ImGui + runtime libraries

---

## Development

### Adding a New Module

1. Create a module class in `native/runtime/src/modules/`
2. Register it in `ModuleManager::loadBuiltinModules()`
3. Implement your hook/patch logic in the module's `enable()` and `disable()` methods
4. Rebuild the runtime with `build-and-patch.sh --build-only`

### Modifying the GUI

The GUI is defined in `native/runtime/src/gui/InGameGui.cpp`:
- `drawModuleList()` – Module list rendering
- `hookedSwap()` – Frame-by-frame rendering logic
- `submitTouch()` – Input processing

Changes are automatically recompiled on the next build.

---

## License & Credits

**E-Client Runtime** — A reverse-engineering and research project for Minecraft Bedrock Edition.

This tool is intended for educational and testing purposes only. Unauthorized use in public servers or competitive play may violate the Minecraft End User License Agreement. Use responsibly.

---

## Support

For issues, questions, or contributions:
- Check logcat for errors: `adb logcat | grep EClient`
- Verify your Minecraft version is 1.21.111
- Ensure your device is ARM64
- Try a clean rebuild if problems persist

---

**Last Updated:** 2026-09-27  
**Supported Minecraft Version:** 1.21.111 ARM64  
**Runtime Version:** 1.0.0
