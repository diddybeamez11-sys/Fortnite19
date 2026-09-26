#pragma once

namespace eclient_runtime::modules::movement {

class NoSlowDown {
public:
    static bool enabled();
    static void update(void* player);
};

} // namespace eclient_runtime::modules::movement
