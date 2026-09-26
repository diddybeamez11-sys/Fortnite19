#pragma once

namespace eclient_runtime::modules::misc {

class NoHurt {
public:
    static bool enabled();
    static void update(void* player);
};

} // namespace eclient_runtime::modules::misc
