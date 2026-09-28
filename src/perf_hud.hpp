#pragma once
#include "power.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>

// The performance card (the XR layer's `stats` key, both scenes): what the viewer, the compositor, the GPU
// and the battery are doing, sampled once a second. It is head-locked (no lazy follow), so while it is
// open the idle-frame gate still skips unchanged frames: the card adds one frame a second, when its text
// changes.
namespace perf {
// Running totals the viewer keeps; the sampler turns two of them into per-second rates.
struct Totals {
    std::uint64_t presented=0, skipped=0, missed=0, captured=0;
    double cpuSeconds=0, compositorSeconds=0;
};

struct Battery {
    bool present=false, charging=false, full=false;
    double watts=-1, hoursLeft=-1;
    int percent=-1;
};

inline double number(const std::filesystem::path& path) {
    std::ifstream file(path); double value=0;
    return (file>>value) ? value : -1;
}

// The system battery (not a peripheral's): power_now, or current_now x voltage_now; the time left from
// energy_now (or charge_now) over the draw while discharging.
inline Battery readBattery(const std::filesystem::path& root="/sys/class/power_supply") {
    Battery b; std::error_code error;
    for (const auto& entry:std::filesystem::directory_iterator(root, error)) {
        const auto& d=entry.path();
        if (power::readFirstLine(d/"type")!="Battery" || power::readFirstLine(d/"scope")=="Device") continue;
        b.present=true;
        const auto status=power::readFirstLine(d/"status");
        b.charging=status=="Charging"; b.full=status=="Full";
        b.percent=int(number(d/"capacity"));
        double watts=number(d/"power_now");
        const double current=number(d/"current_now"), voltage=number(d/"voltage_now");
        if (watts>=0) watts/=1e6;
        else if (current>=0 && voltage>=0) watts=current*voltage/1e12;
        b.watts=watts;
        if (status=="Discharging" && watts>.05) {
            const double energy=number(d/"energy_now");
            const double charge=number(d/"charge_now");
            if (energy>=0) b.hoursLeft=energy/1e6/watts;
            else if (charge>=0 && voltage>0) b.hoursLeft=charge*voltage/1e12/watts;
        }
        return b;
    }
    return b;
}

// utime + stime of /proc/<pid>/stat in seconds; the comm field may hold spaces, so fields count from ')'.
inline double processSeconds(const std::filesystem::path& stat) {
    std::ifstream file(stat); std::string text; std::getline(file, text);
    const auto close=text.rfind(')');
    if (close==std::string::npos) return -1;
    std::istringstream fields(text.substr(close+2));
    std::string skip; unsigned long long user=0, system=0;
    for (int i=0; i<11 && fields>>skip; ++i) {}
    if (!(fields>>user>>system)) return -1;
    static const long ticks=sysconf(_SC_CLK_TCK);
    return double(user+system)/double(ticks>0 ? ticks : 100);
}

// The compositor's pid: the first process whose comm is Hyprland (0 when none).
inline int compositorPid(const std::filesystem::path& proc="/proc") {
    std::error_code error;
    for (const auto& entry:std::filesystem::directory_iterator(proc, error)) {
        const auto name=entry.path().filename().string();
        if (name.empty() || !std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isdigit(c)!=0; })) continue;
        if (power::readFirstLine(entry.path()/"comm")=="Hyprland") return std::stoi(name);
    }
    return 0;
}

// amdgpu's busy percentage for the first card that reports it (-1 elsewhere).
inline int gpuBusy(const std::filesystem::path& drm="/sys/class/drm") {
    std::error_code error;
    for (const auto& entry:std::filesystem::directory_iterator(drm, error)) {
        const auto name=entry.path().filename().string();
        if (!name.starts_with("card") || name.find('-')!=std::string::npos) continue;
        const double busy=number(entry.path()/"device"/"gpu_busy_percent");
        if (busy>=0) return int(busy);
    }
    return -1;
}

// One second's rates from two totals.
struct Rates {
    double presented=0, skipped=0, missed=0, captured=0, cpuPercent=-1, compositorPercent=-1;
};
inline Rates rates(const Totals& before, const Totals& now, double seconds) {
    Rates r;
    if (seconds<=0) return r;
    r.presented=double(now.presented-before.presented)/seconds; r.skipped=double(now.skipped-before.skipped)/seconds;
    r.missed=double(now.missed-before.missed)/seconds; r.captured=double(now.captured-before.captured)/seconds;
    if (now.cpuSeconds>=0 && before.cpuSeconds>=0) r.cpuPercent=100*(now.cpuSeconds-before.cpuSeconds)/seconds;
    if (now.compositorSeconds>=0 && before.compositorSeconds>=0) r.compositorPercent=100*(now.compositorSeconds-before.compositorSeconds)/seconds;
    return r;
}

// Everything the card shows besides the rates, filled by the viewer each sample.
struct Snapshot {
    Rates rates;
    Battery battery;
    unsigned refreshHz=0, sources=0, missedSession=0;
    int gpuBusy=-1;
    double frameP95=0, gpuP99=-1, latchMs=0, predictionMs=0;
    bool direct=false, stereo=false, canvas=false, idleFrames=false, tracking=false, saverEnabled=false, saverActive=false;
};

struct Row { std::string label, value; };

inline std::string fixed(double v, int decimals) { char out[32]; std::snprintf(out, sizeof(out), "%.*f", decimals, v); return out; }
inline std::string duration(double hours) {
    const int minutes=int(std::lround(hours*60));
    return minutes>=60 ? std::to_string(minutes/60)+" h "+std::to_string(minutes%60)+" min" : std::to_string(minutes)+" min";
}

inline std::vector<Row> rows(const Snapshot& s) {
    std::vector<Row> out;
    const auto& r=s.rates;
    std::string frames=fixed(r.presented, 1)+" fps presented";
    if (s.idleFrames) frames+=" · "+fixed(r.skipped, 1)+"/s skipped";
    out.push_back({"Frames", frames});
    std::string display=(s.direct ? (s.stereo ? "stereo " : "direct ") : "window ")+(s.refreshHz ? std::to_string(s.refreshHz)+" Hz" : std::string("vsync"));
    if (s.direct) display+=" · missed "+fixed(r.missed, 1)+"/s ("+std::to_string(s.missedSession)+" total)";
    out.push_back({"Display", display});
    std::string timing="frame p95 "+fixed(s.frameP95, 1)+" ms";
    if (s.gpuP99>=0) timing+=" · GPU p99 "+fixed(s.gpuP99, 1)+" ms";
    if (s.direct) timing+=" · latch "+fixed(s.latchMs, 1)+" ms";
    out.push_back({"Timing", timing});
    out.push_back({"Captures", std::to_string(s.sources)+(s.canvas ? " windows · " : " monitors · ")+fixed(r.captured, 1)+" frames/s"});
    std::string load=r.cpuPercent>=0 ? "viewer "+fixed(r.cpuPercent, 0)+" %" : std::string("viewer ?");
    if (r.compositorPercent>=0) load+=" · Hyprland "+fixed(r.compositorPercent, 0)+" %";
    if (s.gpuBusy>=0) load+=" · GPU busy "+std::to_string(s.gpuBusy)+" %";
    out.push_back({"CPU / GPU", load});
    const auto& b=s.battery;
    if (b.present) {
        std::string battery=b.percent>=0 ? std::to_string(b.percent)+" %" : std::string("?");
        battery+=b.charging ? " charging" : b.full ? " full" : " on battery";
        // A full battery on external power reports its own reading, not what the system draws.
        if (b.watts>=0 && !b.full) battery+=" · "+fixed(b.watts, 1)+(b.charging ? " W in" : " W draw");
        if (b.hoursLeft>=0) battery+=" · "+duration(b.hoursLeft)+" left";
        out.push_back({"Battery", battery});
    }
    out.push_back({"Saver", !s.saverEnabled ? "off" : s.saverActive ? "active: captures ≤ "+std::to_string(power::captureCapHz)+" Hz" : "on, external power"});
    out.push_back({"Tracking", s.tracking ? "live · prediction "+fixed(s.predictionMs, 1)+" ms" : "waiting / stale"});
    return out;
}

}
