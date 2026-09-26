#pragma once

namespace eclient_runtime::modules::visual {

class FullBright {
public:
    static bool enabled();
    static void update(void* player);
};

} // namespace eclient_runtime::modules::visual
