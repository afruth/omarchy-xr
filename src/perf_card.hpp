#pragma once
#include "canvas_overlay.hpp"
#include "perf_hud.hpp"
#include <string>
#include <vector>

// The performance card's raster: a title and one label/value line per perf::Row.
namespace perf {
constexpr int width=640, rowHeight=30, top=58, maxHeight=top+12*rowHeight;
inline std::string key(const std::vector<Row>& rows, const canvas::overlay::Style& s) {
    std::string out=canvas::overlay::colorKey(s.accent);
    for (const auto& r:rows) out+="\n"+r.label+"\t"+r.value;
    return out;
}
inline int paint(cairo_t* cr, const std::vector<Row>& rows, const canvas::overlay::Style& s) {
    const int height=top+int(rows.size())*rowHeight+14;
    canvas::overlay::card(cr, width, height, s);
    notifications::text(cr, "Performance", s.text, 24, 16, 400, 22, 1, true);
    for (size_t i=0; i<rows.size(); ++i) {
        const int y=top+int(i)*rowHeight;
        notifications::text(cr, rows[i].label, s.accent, 24, y, 130, 17, 1, true);
        notifications::text(cr, rows[i].value, s.text, 158, y, width-182, 17, 1);
    }
    return height;
}
}
