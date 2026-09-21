#pragma once
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

struct PanelLayout {
    std::string output;
    float x = 0, y = 0, width = 1920, height = 1080;
    float curvature = 0;
    float brightness = 100;
};
inline std::vector<PanelLayout> readLayout(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open layout: " + path);
    std::vector<PanelLayout> panels;
    std::unordered_set<std::string> names;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream row(line);
        PanelLayout p;
        std::string extra;
        if (!(row >> p.output >> p.x >> p.y >> p.width >> p.height) ||
            !std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 1000000 || std::abs(p.y) > 1000000 ||
            !std::isfinite(p.width) || !std::isfinite(p.height) ||
            p.width < 1 || p.height < 1 || p.width > 16384 || p.height > 16384 ||
            !names.insert(p.output).second)
            throw std::runtime_error("Invalid or duplicate panel in layout: " + line);
        row >> std::ws;
        if (!row.eof()) {
            if (!(row >> p.curvature) || !std::isfinite(p.curvature) || p.curvature < 0 || p.curvature > 100)
                throw std::runtime_error("Surface curvature must be 0..100");
        }
        row >> std::ws;
        if (!row.eof()) {
            if (!(row >> p.brightness) || row >> extra || !std::isfinite(p.brightness) || p.brightness < 1 || p.brightness > 100)
                throw std::runtime_error("Brightness must be 1..100");
        }
        panels.push_back(p);
    }
    if (panels.empty()) throw std::runtime_error("Layout has no panels");
    return panels;
}
