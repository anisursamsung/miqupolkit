#pragma once

#include <string>

namespace miqu {

struct PolkitConfig {
    int width = 440;
    bool dim_backdrop = true;
    bool close_on_click_outside = false;
    bool show_user_identity = true;
    int icon_size = 32;

    static PolkitConfig& get();
    void load(const std::string& custom_path = "");
};

} // namespace miqu
