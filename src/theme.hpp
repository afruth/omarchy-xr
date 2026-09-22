#pragma once
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

// The selected monitor's border follows the Omarchy theme's accent colour. Omarchy keeps the
// current theme under ~/.local/state/omarchy/current/theme, whose colors.toml has `accent = "#rrggbb"`.
namespace theme {
using Rgb=std::array<float,3>;
inline std::optional<Rgb> parseAccent(const std::string& toml) {
    std::string line;
    for (size_t at=0; at<toml.size();) {
        const size_t end=toml.find('\n', at);
        line=toml.substr(at, end==std::string::npos ? std::string::npos : end-at);
        at=end==std::string::npos ? toml.size() : end+1;
        const size_t key=line.find_first_not_of(" \t");
        if (key==std::string::npos || line.compare(key, 6, "accent")!=0) continue;
        const size_t hash=line.find('#', key+6);
        if (hash==std::string::npos || hash+7>line.size()) continue;
        const std::string hex=line.substr(hash+1, 6);
        if (hex.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos) continue;
        Rgb rgb{};
        for (int i=0; i<3; ++i) rgb[i]=float(std::strtol(hex.substr(i*2, 2).c_str(), nullptr, 16))/255;
        return rgb;
    }
    return std::nullopt;
}
inline std::string colorsPath() {
    const char* state=std::getenv("XDG_STATE_HOME");
    const std::filesystem::path base=state && *state ? std::filesystem::path(state) : std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME") : "") / ".local/state";
    return (base/"omarchy/current/theme/colors.toml").string();
}
// Re-reads the theme file when it changes, at most every two seconds. Falls back to the stock blue.
struct Accent {
    Rgb rgb{.35f,.65f,1.f};
    std::string path=colorsPath();
    std::filesystem::file_time_type version{};
    double nextCheck=0;
    bool loaded=false;
    void update(double now) {
        if (now<nextCheck) return;
        nextCheck=now+2;
        std::error_code missing;
        const auto stamp=std::filesystem::last_write_time(path, missing);
        if (missing) { if (loaded) { rgb={.35f,.65f,1.f}; loaded=false; } return; }
        if (loaded && stamp==version) return;
        version=stamp;
        std::ifstream file(path);
        std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (const auto accent=parseAccent(text)) { rgb=*accent; loaded=true; }
    }
};
}
