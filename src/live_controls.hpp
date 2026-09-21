#pragma once
#include "async_file.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <cmath>
#include <ctime>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// Mailboxes sit beside the pose socket. Hover and the heartbeat are also
// mirrored to OMARCHY_XR_MIRROR_STATE temporarily, until the Lua adapter
// installed by make install-controls reads the runtime path. Hyprland writes
// cumulative motion, so coalescing never drops swipe distance.
class LiveControls {
    std::string path, mirror, session;
    std::string controlsSeen, panSeen;
    timespec controlsStamp{}, panStamp{};
    double previousZoom=0, previousPanX=0, previousPanY=0;
    unsigned long long panSerial=0, panId=0;
    bool hoverEnabled=false;
    std::string hoverOutput;
    long hoverStamp=0;
    static long bootSeconds() {
        timespec now{};
        clock_gettime(CLOCK_BOOTTIME, &now);
        return now.tv_sec;
    }
    static bool stampFresh(long stamp) {
        if (stamp > 1000000000L) {
            const auto now = std::time(nullptr);
            return stamp <= now && now - stamp <= 2;
        }
        const auto now = bootSeconds();
        return stamp <= now && now - stamp <= 2;
    }
    static bool newer(const std::string& file, timespec& seen) {
        struct stat st{};
        if (stat(file.c_str(), &st) != 0) { seen = {}; return false; }
        if (st.st_mtim.tv_sec == seen.tv_sec && st.st_mtim.tv_nsec == seen.tv_nsec) return false;
        seen = st.st_mtim;
        return true;
    }
    std::string existing(const std::string& suffix) const {
        struct stat st{};
        if (stat((path + suffix).c_str(), &st) == 0) return path + suffix;
        if (!mirror.empty() && stat((mirror + suffix).c_str(), &st) == 0) return mirror + suffix;
        return path + suffix;
    }
    static void writeFile(const std::string& target, const std::string& body) {
        AsyncFile::instance().write(target, body);
    }
    void updatePan() {
        panX = panY = 0; panStarted = false;
        const auto filePath = existing(".pan");
        if (filePath != panSeen) { panSeen = filePath; panStamp = {}; }
        struct stat st{};
        if (stat(filePath.c_str(), &st) != 0) { panActive = false; panStamp = {}; return; }
        if (st.st_mtim.tv_sec == panStamp.tv_sec && st.st_mtim.tv_nsec == panStamp.tv_nsec) return;
        panStamp = st.st_mtim;
        panActive = false;
        std::ifstream file(filePath); std::string first, owner, extra;
        unsigned long long seq, id; double x, y; int active; long stamp;
        if (!(file >> first)) return;
        if (first == "v2") { if (!(file >> owner)) return; }
        else owner = first;
        if (!(file >> seq >> id >> x >> y >> active >> stamp) || file >> extra || owner != session ||
            !std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 1e9 || std::abs(y) > 1e9 ||
            (active != 0 && active != 1) || !stampFresh(stamp)) return;
        panActive = active;
        if (seq == panSerial) return;
        panStarted = id != panId;
        if (panStarted) { previousPanX = previousPanY = 0; panId = id; }
        panX = std::clamp(x - previousPanX, -1000., 1000.);
        panY = std::clamp(y - previousPanY, -1000., 1000.);
        previousPanX = x; previousPanY = y; panSerial = seq;
    }
    unsigned long long serial = 0, fitSerial = 0, hoverSerial = 0;
    long heartbeat = 0;
public:
    double zoom = 0, panX = 0, panY = 0;
    bool panStarted = false, panActive = false;
    int fit = 0;
    explicit LiveControls(const std::string& pose) : path(pose.empty() ? "" : pose + ".controls"), session(std::to_string(getpid())) {
        if (const char* mirrored = std::getenv("OMARCHY_XR_MIRROR_STATE")) mirror = mirrored;
        if (mirror == path) mirror.clear();
        if (!path.empty()) { unlink(path.c_str()); unlink((path + ".pan").c_str()); update(); }
    }
    ~LiveControls() {
        AsyncFile::instance().flush();
        if (path.empty()) return;
        for (const auto& base : {path, mirror}) {
            if (base.empty()) continue;
            unlink((base + ".pan").c_str()); unlink((base + ".active").c_str()); unlink(base.c_str());
            unlink((base + ".hover").c_str()); unlink((base + ".pointer").c_str());
        }
    }
    void publishHover(bool enabled, const std::string& output, float u, float v) {
        if (path.empty()) return;
        const auto now = bootSeconds();
        if (enabled == hoverEnabled && output == hoverOutput && now == hoverStamp) return;
        hoverEnabled = enabled; hoverOutput = output; hoverStamp = now;
        const auto body = session + ' ' + std::to_string(++hoverSerial) + ' ' + (enabled ? '1' : '0') + ' ' +
            (output.empty() ? "-" : output) + ' ';
        std::ostringstream values;
        values << body << std::setprecision(9) << u << ' ' << v << '\n';
        writeFile(path + ".hover", "v2 " + values.str());
        if (!mirror.empty()) writeFile(mirror + ".hover", values.str());
    }
    void update() {
        zoom = 0; fit = 0; if (path.empty()) return;
        updatePan();
        const auto now = bootSeconds();
        if (now != heartbeat) {
            const auto body = session + ' ' + std::to_string(now) + '\n';
            writeFile(path + ".active", body);
            if (!mirror.empty()) writeFile(mirror + ".active", body);
            heartbeat = now;
        }
        const auto filePath = existing("");
        if (filePath != controlsSeen) { controlsSeen = filePath; controlsStamp = {}; }
        if (!newer(filePath, controlsStamp)) return;
        std::ifstream file(filePath);
        std::string first, owner, extra; unsigned long long nextSerial, nextFit; double total; int mode;
        if (!(file >> first)) return;
        if (first == "v2") { if (!(file >> owner)) return; }
        else owner = first;
        if (!(file >> nextSerial >> total >> nextFit >> mode) || file >> extra || owner != session ||
            !std::isfinite(total) || std::abs(total) > 1e9 || mode < 0 || mode > 5 || nextSerial == serial) return;
        if (nextFit != fitSerial) { fit = mode; fitSerial = nextFit; }
        else zoom = std::clamp(total - previousZoom, -4., 4.);
        serial = nextSerial; previousZoom = total;
    }
};
