#pragma once

#include <jni.h>

namespace eclient_runtime::input {

// Installs the input hooks used by the in-game GUI. The JNI registration hook
// is installed as early as possible so dynamically registered Minecraft input
// callbacks can be discovered before the game registers them.
bool installJniRegistrationHook(JavaVM* vm);
bool initialize();
void shutdown();

} // namespace eclient_runtime::input
