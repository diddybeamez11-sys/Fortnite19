#pragma once

namespace eclient_runtime::modules::player {

class NoEmoteCooldown {
public:
    static bool enabled();
    static void update(void* player);
};

} // namespace eclient_runtime::modules::player
