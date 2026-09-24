#include "config.hpp"
#include <miqutoolkit/core/config.hpp>
#include <miqutoolkit/core/fs_utils.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <iostream>

namespace miqu {

namespace fs = std::filesystem;

static PolkitConfig s_polkit_config;

PolkitConfig& PolkitConfig::get() {
    return s_polkit_config;
}

static std::string trim_str(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n\"'");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n\"'");
    return str.substr(first, (last - first + 1));
}

static bool parse_bool(const std::string& val) {
    std::string s = val;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return (s == "true" || s == "1" || s == "yes" || s == "on");
}

void PolkitConfig::load(const std::string& custom_path) {
    std::string target_path;
    if (!custom_path.empty() && fs::exists(custom_path)) {
        target_path = custom_path;
    } else {
        std::string user_cfg_dir = FsUtils::get_user_config_dir("miqupolkit");
        if (!user_cfg_dir.empty()) {
            std::string p = user_cfg_dir + "/miqupolkit.conf";
            if (fs::exists(p)) {
                target_path = p;
            }
        }
    }

    if (target_path.empty() || !fs::exists(target_path)) {
        return;
    }

    // 1. Synchronize toolkit theme
    Config::get()->load_from_file(target_path);

    // 2. Parse dialog specific options
    std::ifstream file(target_path);
    if (!file.is_open()) return;

    std::string line;
    std::string current_section = "";

    while (std::getline(file, line)) {
        line = trim_str(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        if (line.front() == '[' && line.back() == ']') {
            current_section = trim_str(line.substr(1, line.size() - 2));
            std::transform(current_section.begin(), current_section.end(), current_section.begin(), ::tolower);
            continue;
        }

        auto eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;

        std::string key = trim_str(line.substr(0, eq_pos));
        std::string val = trim_str(line.substr(eq_pos + 1));
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);

        size_t c_pos = val.find('#');
        if (c_pos != std::string::npos) {
            val = trim_str(val.substr(0, c_pos));
        }

        if (key == "width") {
            try { width = std::max(280, std::stoi(val)); } catch (...) {}
        } else if (key == "dim_backdrop" || key == "dim") {
            dim_backdrop = parse_bool(val);
        } else if (key == "close_on_click_outside") {
            close_on_click_outside = parse_bool(val);
        } else if (key == "show_user_identity") {
            show_user_identity = parse_bool(val);
        } else if (key == "icon_size") {
            try { icon_size = std::max(16, std::stoi(val)); } catch (...) {}
        }
    }
}

} // namespace miqu
