#pragma once
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

// Battery saver (Studio, off by default): while the computer runs on battery, captures are capped so the
// compositor renders and copies fewer frames. power.tsv sits beside the layout: `power-v1 <batterySaver 0|1>`.
namespace power {
inline std::optional<bool> parseSettings(const std::string& line) {
    std::istringstream in(line); std::string tag, extra; int saver=-1;
    if (!(in>>tag>>saver) || tag!="power-v1" || (saver!=0 && saver!=1) || (in>>extra)) return std::nullopt;
    return saver==1;
}

inline std::string readFirstLine(const std::filesystem::path& path) {
    std::ifstream file(path); std::string line; std::getline(file, line); return line;
}

// On battery: no external supply (Mains, USB, USB-C) is online and a system battery reports Discharging.
// Peripheral batteries (scope Device: a mouse, a headset) do not count. No battery (a desktop) or an
// unreadable sysfs counts as external power.
inline bool onBattery(const std::filesystem::path& root="/sys/class/power_supply") {
    std::error_code error; bool discharging=false;
    for (const auto& entry:std::filesystem::directory_iterator(root, error)) {
        const auto type=readFirstLine(entry.path()/"type");
        if (readFirstLine(entry.path()/"scope")=="Device") continue;
        if (type=="Battery") { if (readFirstLine(entry.path()/"status")=="Discharging") discharging=true; }
        else if (readFirstLine(entry.path()/"online")=="1") return false;
    }
    return discharging;
}

// The caps while the saver is active: the top capture rate and the share of the canvas pixel budget.
constexpr unsigned captureCapHz=30;
constexpr float budgetScale=.5f;

struct Saver {
    bool enabled=false, battery=false;
    bool active() const { return enabled && battery; }
    unsigned captureHz(unsigned requested) const { return active() ? std::min(requested, captureCapHz) : requested; }
    float budget() const { return active() ? budgetScale : 1.f; }
};
}
