#pragma once

namespace eclient_runtime::modules::combat {

class AutoClickMine {
public:
    static bool enabled();
    static void update(void* player);
};

} // namespace eclient_runtime::modules::combat
