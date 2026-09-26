#pragma once

namespace eclient_runtime::modules::visual {

class NoHurtCam {
public:
    static bool enabled();
    static void update(void* player);
};

} // namespace eclient_runtime::modules::visual
