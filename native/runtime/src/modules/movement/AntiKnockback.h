#pragma once

namespace eclient_runtime::modules::movement {

class AntiKnockback {
public:
    static bool enabled();
    static void update(void* player);
};

} // namespace eclient_runtime::modules::movement
