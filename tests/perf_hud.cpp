#include "perf_hud.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace {
int failures=0;
void expect(bool ok, const std::string& what) { if (!ok) { std::cerr << what << '\n'; ++failures; } }
void write(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << text << '\n';
}
std::string value(const std::vector<perf::Row>& rows, const std::string& label) {
    for (const auto& r:rows) if (r.label==label) return r.value;
    return "<missing>";
}
}

int main() {
    const auto root=std::filesystem::temp_directory_path()/("omxr-perf-"+std::to_string(getpid()));
    std::filesystem::remove_all(root);
    {
        // power_now in µW, energy_now in µWh: 12.5 W from 25 Wh is two hours.
        const auto s=root/"supply";
        write(s/"BAT0"/"type", "Battery"); write(s/"BAT0"/"status", "Discharging"); write(s/"BAT0"/"capacity", "64");
        write(s/"BAT0"/"power_now", "12500000"); write(s/"BAT0"/"energy_now", "25000000");
        write(s/"mouse"/"type", "Battery"); write(s/"mouse"/"scope", "Device"); write(s/"mouse"/"capacity", "5");
        const auto b=perf::readBattery(s);
        expect(b.present && b.percent==64 && std::abs(b.watts-12.5)<1e-9 && std::abs(b.hoursLeft-2)<1e-9 && !b.charging, "power_now battery");
        // No power_now: current_now (µA) x voltage_now (µV); charge_now (µAh) for the time left.
        std::filesystem::remove(s/"BAT0"/"power_now"); std::filesystem::remove(s/"BAT0"/"energy_now");
        write(s/"BAT0"/"current_now", "1000000"); write(s/"BAT0"/"voltage_now", "15000000"); write(s/"BAT0"/"charge_now", "3000000");
        const auto c=perf::readBattery(s);
        expect(std::abs(c.watts-15)<1e-9 && std::abs(c.hoursLeft-3)<1e-9, "current x voltage battery");
        write(s/"BAT0"/"status", "Charging");
        const auto d=perf::readBattery(s);
        expect(d.charging && d.hoursLeft<0, "A charging battery has no time left");
        expect(!perf::readBattery(root/"none").present, "No supply directory, no battery");
    }
    {
        // Field 14/15 after a comm with spaces and parentheses.
        write(root/"stat", "42 (Web (Content) x) S 1 2 3 4 5 6 7 8 9 10 200 100 0 0 20 0 1 0");
        const long ticks=sysconf(_SC_CLK_TCK);
        expect(std::abs(perf::processSeconds(root/"stat")-300.0/double(ticks))<1e-9, "utime+stime from /proc stat");
        expect(perf::processSeconds(root/"missing")<0, "A missing stat file is unknown");
        write(root/"proc"/"17"/"comm", "bash"); write(root/"proc"/"99"/"comm", "Hyprland"); write(root/"proc"/"self"/"comm", "Hyprland");
        expect(perf::compositorPid(root/"proc")==99, "The compositor pid comes from a numeric /proc entry");
        write(root/"drm"/"card1"/"device"/"gpu_busy_percent", "37"); write(root/"drm"/"card1-DP-1"/"device"/"gpu_busy_percent", "99");
        expect(perf::gpuBusy(root/"drm")==37 && perf::gpuBusy(root/"none")==-1, "amdgpu busy percent from the card, not a connector");
    }
    {
        const perf::Totals a{100, 50, 2, 1000, 10, 20}, b{160, 110, 3, 1060, 10.25, 20.5};
        const auto r=perf::rates(a, b, 2);
        expect(r.presented==30 && r.skipped==30 && r.missed==.5 && r.captured==30 && std::abs(r.cpuPercent-12.5)<1e-9 && std::abs(r.compositorPercent-25)<1e-9, "rates over two seconds");
        const auto unknown=perf::rates({0, 0, 0, 0, -1, -1}, {0, 0, 0, 0, 1, 1}, 1);
        expect(unknown.cpuPercent<0 && unknown.compositorPercent<0, "Unknown CPU stays unknown");
    }
    {
        perf::Snapshot s;
        s.rates={59.8, 0, 0, 12, 4, 7}; s.direct=s.stereo=true; s.refreshHz=60; s.missedSession=3; s.sources=2; s.gpuBusy=23;
        s.frameP95=16.9; s.gpuP99=2.4; s.latchMs=3.5; s.idleFrames=true; s.tracking=true; s.predictionMs=11.2;
        s.battery={true, false, false, 12.5, 2.5, 64}; s.saverEnabled=s.saverActive=true;
        const auto rows=perf::rows(s);
        expect(value(rows, "Frames")=="59.8 fps presented · 0.0/s skipped", "Frames row: "+value(rows, "Frames"));
        expect(value(rows, "Display")=="stereo 60 Hz · missed 0.0/s (3 total)", "Display row: "+value(rows, "Display"));
        expect(value(rows, "Timing")=="frame p95 16.9 ms · GPU p99 2.4 ms · latch 3.5 ms", "Timing row: "+value(rows, "Timing"));
        expect(value(rows, "Captures")=="2 monitors · 12.0 frames/s", "Captures row: "+value(rows, "Captures"));
        expect(value(rows, "CPU / GPU")=="viewer 4 % · Hyprland 7 % · GPU busy 23 %", "Load row: "+value(rows, "CPU / GPU"));
        expect(value(rows, "Battery")=="64 % on battery · 12.5 W draw · 2 h 30 min left", "Battery row: "+value(rows, "Battery"));
        auto full=s; full.battery={true, false, true, 27.7, -1, 100};
        expect(value(perf::rows(full), "Battery")=="100 % full", "A full battery shows no watts");
        auto charging=s; charging.battery={true, true, false, 30, -1, 80};
        expect(value(perf::rows(charging), "Battery")=="80 % charging · 30.0 W in", "A charging battery shows the charge rate");
        expect(value(rows, "Saver")=="active: captures ≤ 30 Hz", "Saver row: "+value(rows, "Saver"));
        expect(value(rows, "Tracking")=="live · prediction 11.2 ms", "Tracking row: "+value(rows, "Tracking"));
        perf::Snapshot desk;
        const auto plain=perf::rows(desk);
        expect(value(plain, "Battery")=="<missing>" && value(plain, "Display")=="window vsync" && value(plain, "Frames")=="0.0 fps presented", "A desktop without battery or direct output");
    }
    std::filesystem::remove_all(root);
    if (failures) return 1;
    std::cout << "Performance card: battery, /proc and GPU sampling, rates and rows passed" << std::endl;
    return 0;
}
