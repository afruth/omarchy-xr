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
    unsigned long long serial = 0, fitSerial = 0, hoverSerial = 0, hoverPointerSerial = 0;
    long heartbeat = 0, notificationStamp = 0;
    std::string notificationPublished;
    std::string paneSeen; timespec paneStamp{};
    timespec focusStamp{};
    unsigned long long focusSerial=0;
    void updateFocus() {
        const auto filePath=path+".focus";
        if (!newer(filePath, focusStamp)) return;
        std::ifstream file(filePath);
        std::string version, owner, name, extra;
        unsigned long long seq; long stamp;
        if (!(file>>version>>owner>>seq>>name>>stamp) || file>>extra || version!="v1" || owner!=session
            || !seq || seq<=focusSerial || !stampFresh(stamp) || !name.starts_with("OMXR-") || name.size()>256
            || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos) return;
        focusSerial=seq; focusOutput=name;
    }
    // The adapter publishes the active window's rectangle on its monitor: v1 owner serial name x y w h stamp,
    // or v1 owner serial - stamp when the active window is not on an XR output.
    void updatePane() {
        const auto filePath = existing(".pane");
        if (filePath != paneSeen) { paneSeen = filePath; paneStamp = {}; }
        struct stat st{};
        if (stat(filePath.c_str(), &st) != 0) { paneValid = false; paneStamp = {}; return; }
        if (st.st_mtim.tv_sec == paneStamp.tv_sec && st.st_mtim.tv_nsec == paneStamp.tv_nsec) return;
        paneStamp = st.st_mtim;
        paneValid = false;
        std::ifstream file(filePath); std::string version, owner, name, extra;
        unsigned long long seq; long stamp;
        if (!(file >> version >> owner >> seq >> name) || version != "v1" || owner != session) return;
        if (name == "-") { file >> stamp; return; }
        double x, y, w, h;
        if (!(file >> x >> y >> w >> h >> stamp) || file >> extra || !stampFresh(stamp)
            || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h)
            || w <= 0 || h <= 0 || w > 32768 || h > 32768 || std::abs(x) > 1e6 || std::abs(y) > 1e6) return;
        paneOutput = name; paneX = float(x); paneY = float(y); paneW = float(w); paneH = float(h); paneValid = true;
    }
public:
    std::string focusOutput, notificationTarget;
    std::string paneOutput;
    float paneX = 0, paneY = 0, paneW = 0, paneH = 0;
    bool paneValid = false;
    double zoom = 0, panX = 0, panY = 0;
    bool panStarted = false, panActive = false;
    int fit = 0;
    explicit LiveControls(const std::string& pose) : path(pose.empty() ? "" : pose + ".controls"), session(std::to_string(getpid())) {
        if (const char* mirrored = std::getenv("OMARCHY_XR_MIRROR_STATE")) mirror = mirrored;
        if (mirror == path) mirror.clear();
        if (!path.empty()) { unlink(path.c_str()); unlink((path + ".pan").c_str()); unlink((path + ".focus").c_str()); update(); }
    }
    ~LiveControls() {
        AsyncFile::instance().flush();
        if (path.empty()) return;
        for (const auto& base : {path, mirror}) {
            if (base.empty()) continue;
            unlink((base + ".pan").c_str()); unlink((base + ".pane").c_str()); unlink((base + ".active").c_str()); unlink(base.c_str());
            unlink((base + ".hover").c_str()); unlink((base + ".pointer").c_str());
            unlink((base + ".focus").c_str());unlink((base + ".notification").c_str());
        }
    }
    // pointerSerial changes once per gaze dwell; pointerX/Y are that dwell's monitor pixel
    // coordinates. The adapter warps the desktop pointer there exactly once per serial.
    void publishHover(bool enabled, const std::string& output, float u, float v, unsigned pointerSerial=0, float pointerX=0, float pointerY=0) {
        if (path.empty()) return;
        const auto now = bootSeconds();
        if (enabled == hoverEnabled && output == hoverOutput && pointerSerial == hoverPointerSerial && now == hoverStamp) return;
        hoverEnabled = enabled; hoverOutput = output; hoverStamp = now; hoverPointerSerial = pointerSerial;
        const auto body = session + ' ' + std::to_string(++hoverSerial) + ' ' + (enabled ? '1' : '0') + ' ' +
            (output.empty() ? "-" : output) + ' ';
        std::ostringstream values;
        values << body << std::setprecision(9) << u << ' ' << v;
        std::ostringstream pointer;
        pointer << values.str() << ' ' << pointerSerial << ' ' << std::setprecision(9) << pointerX << ' ' << pointerY << '\n';
        writeFile(path + ".hover", "v3 " + pointer.str());
        if (!mirror.empty()) writeFile(mirror + ".hover", values.str() + '\n');
    }
    void publishNotification(const std::string& identity) {
        if(path.empty())return;
        const auto now=bootSeconds();
        if(identity==notificationPublished && now==notificationStamp)return;
        notificationPublished=identity;notificationStamp=now;
        std::string token;
        constexpr char digits[]="0123456789abcdef";
        for(unsigned char c:identity){token+=digits[c>>4];token+=digits[c&15];}
        writeFile(path+".notification","v1 "+session+" "+(token.empty()?"-":token)+" "+std::to_string(now)+"\n");
    }
    static std::string decodeTarget(const std::string& token) {
        if(token.empty() || token.size()>1100 || token.size()%2 || token.find_first_not_of("0123456789abcdef")!=std::string::npos)return {};
        std::string identity;
        for(size_t i=0;i<token.size();i+=2)identity+=char(std::stoul(token.substr(i,2),nullptr,16));
        return identity;
    }
    void update() {
        zoom = 0; fit = 0; focusOutput.clear();notificationTarget.clear(); if (path.empty()) return;
        updatePan();
        updatePane();
        updateFocus();
        const auto now = bootSeconds();
        if (now != heartbeat) {
            writeFile(path + ".active", session + ' ' + std::to_string(now) + '\n');
            if (!mirror.empty()) writeFile(mirror + ".active", session + ' ' + std::to_string(std::time(nullptr)) + '\n');
            heartbeat = now;
        }
        const auto filePath = existing("");
        if (filePath != controlsSeen) { controlsSeen = filePath; controlsStamp = {}; }
        if (!newer(filePath, controlsStamp)) return;
        std::ifstream file(filePath);
        std::string first, owner, extra; unsigned long long nextSerial, nextFit; double total; int mode;
        if (!(file >> first)) return;
        if (first == "v2" || first == "v3") { if (!(file >> owner)) return; }
        else owner = first;
        if (!(file >> nextSerial >> total >> nextFit >> mode) || owner != session ||
            !std::isfinite(total) || std::abs(total) > 1e9 || mode < 0 || mode > 7 || nextSerial <= serial) return;
        std::string target;long stamp=0;
        if(first=="v3") {
            std::string token;
            if(!(file>>token>>stamp))return;
            target=decodeTarget(token);
        }
        if(file>>extra)return;
        if(mode>=6 && (first!="v3" || target.empty() || !stampFresh(stamp)))return;
        if (nextFit != fitSerial) { fit = mode; fitSerial = nextFit;notificationTarget=target; }
        else zoom = std::clamp(total - previousZoom, -4., 4.);
        serial = nextSerial; previousZoom = total;
    }
};
