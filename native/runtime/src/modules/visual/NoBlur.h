#pragma once

namespace eclient_runtime::modules::visual {

class NoBlur {
public:
    static bool enabled();
    static void update(void* player);
};

} // namespace eclient_runtime::modules::visual
