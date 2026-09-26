#pragma once
#include "async_file.hpp"
#include "hex_token.hpp"
#include "window_list.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <cmath>
#include <ctime>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <vector>

// Mailboxes sit beside the pose socket. Hover and the heartbeat are also
// mirrored to OMARCHY_XR_MIRROR_STATE temporarily, until the Lua adapter
// installed by make install-controls reads the runtime path. Hyprland writes
// cumulative motion, so coalescing never drops swipe distance. In canvas mode
// (docs/infinite-canvas-plan.md §3.4, §6.2) the adapter also publishes the window
// list and the cursor, `.hover` is v4 and `.focus` names window addresses.
class LiveControls {
    std::string path, mirror, session;
    std::string controlsSeen;
    timespec controlsStamp{};
    double previousZoom=0;
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
    // Cumulative pointer travel (.pan, and .drag in canvas mode): v2 owner seq id x y active stamp; a new
    // id starts a gesture, and each update yields the delta since the last line, clamped to +-1000.
    struct Cumulative { std::string seen; timespec stamp{}; double prevX=0, prevY=0; unsigned long long serial=0, id=0; };
    Cumulative pan, drag;
    void updateCumulative(const char* suffix, Cumulative& c, double& x, double& y, bool& started, bool& active) {
        x = y = 0; started = false;
        const auto filePath = existing(suffix);
        if (filePath != c.seen) { c.seen = filePath; c.stamp = {}; }
        struct stat st{};
        if (stat(filePath.c_str(), &st) != 0) { active = false; c.stamp = {}; return; }
        if (st.st_mtim.tv_sec == c.stamp.tv_sec && st.st_mtim.tv_nsec == c.stamp.tv_nsec) return;
        c.stamp = st.st_mtim;
        active = false;
        std::ifstream file(filePath); std::string first, owner, extra;
        unsigned long long seq, id; double totalX, totalY; int on; long stamp;
        if (!(file >> first)) return;
        if (first == "v2") { if (!(file >> owner)) return; }
        else owner = first;
        if (!(file >> seq >> id >> totalX >> totalY >> on >> stamp) || file >> extra || owner != session ||
            !std::isfinite(totalX) || !std::isfinite(totalY) || std::abs(totalX) > 1e9 || std::abs(totalY) > 1e9 ||
            (on != 0 && on != 1) || !stampFresh(stamp)) return;
        active = on;
        if (seq == c.serial) return;
        started = id != c.id;
        if (started) { c.prevX = c.prevY = 0; c.id = id; }
        x = std::clamp(totalX - c.prevX, -1000., 1000.);
        y = std::clamp(totalY - c.prevY, -1000., 1000.);
        c.prevX = totalX; c.prevY = totalY; c.serial = seq;
    }
    unsigned long long serial = 0, fitSerial = 0, hoverSerial = 0, hoverPointerSerial = 0;
    long heartbeat = 0, notificationStamp = 0;
    std::string notificationPublished;
    std::string paneSeen; timespec paneStamp{};
    timespec focusStamp{};
    unsigned long long focusSerial=0;
    unsigned long long windowsSeq=0, cursorSeq=0;
    timespec windowsStamp{}, cursorStamp{};
    // Search prompt (§5.4): .prompt asks the Quickshell prompt to open (seq counts every open/close,
    // rewritten each heartbeat while open), .search answers it, .fill asks Lua to resize (M4).
    unsigned long long promptSeq=0, searchSeq=0, fillSeq=0;
    bool promptOpen=false, takeover=true;
    std::string promptOutput;
    timespec searchStamp{};
    // .tiers (M5): the sliver set last written, sorted; tiersAt is its monotonic write time (rate limit).
    unsigned long long tiersSeq=0;
    std::vector<std::uint64_t> tiersSet;
    double tiersAt=-1e9;
    // Canvas focus names a window address, canonicalised to Record::name(); monitor focus an OMXR- output.
    bool focusName(const std::string& name) const {
        if (canvasMode) return bool(::windows::parseAddress(name));
        return name.starts_with("OMXR-") && name.size()<=256
            && name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==std::string::npos;
    }
    void updateFocus() {
        const auto filePath=path+".focus";
        if (!newer(filePath, focusStamp)) return;
        std::ifstream file(filePath);
        std::string version, owner, name, extra;
        unsigned long long seq; long stamp;
        if (!(file>>version>>owner>>seq>>name>>stamp) || file>>extra || version!="v1" || owner!=session
            || !seq || seq<=focusSerial || !stampFresh(stamp) || !focusName(name)) return;
        focusSerial=seq;
        if (canvasMode) { ::windows::Record r; r.address=*::windows::parseAddress(name); focusOutput=r.name(); }
        else focusOutput=name;
    }
    static std::string readAll(const std::string& file) {
        std::ifstream in(file);
        return std::string((std::istreambuf_iterator<char>(in)), {});
    }
    // The adapter's window list (§3.1): this session's, strictly newer and fresh; canvas mode only.
    void updateWindows() {
        if (!canvasMode || !newer(path + ".windows", windowsStamp)) return;
        auto list = ::windows::parse(readAll(path + ".windows"));
        if (!list || list->owner != session || list->seq <= windowsSeq || !stampFresh(list->stamp)) return;
        windowsSeq = list->seq; windows = std::move(*list);
    }
    // The compositor cursor and its overflow beyond the staged window (§3.4); canvas mode only.
    void updateCursor() {
        if (!canvasMode || !newer(path + ".cursor", cursorStamp)) return;
        const auto line = ::windows::parseCursor(readAll(path + ".cursor"));
        if (!line || line->owner != session || line->seq <= cursorSeq || !stampFresh(line->stamp)) return;
        cursorSeq = line->seq; cursor = *line;
    }
    // This session's prompt, answering the current .prompt request with a newer edit, fresh; canvas only.
    // Lines overwritten between two polls are not lost keys: the key log keeps the keys this reader has
    // not seen (editSeq beyond the last one read), oldest first.
    void updateSearch() {
        if (!canvasMode || !newer(path + ".search", searchStamp)) return;
        auto line = ::windows::parseSearch(readAll(path + ".search"));
        if (!line || line->owner != session || !promptSeq || line->promptSeq != promptSeq || line->editSeq <= searchSeq || !stampFresh(line->stamp)) return;
        const auto unseen = line->editSeq - searchSeq;
        if (line->keys.size() > unseen) line->keys.erase(line->keys.begin(), line->keys.end() - long(unseen));
        searchSeq = line->editSeq; search = std::move(*line);
    }
    void writeTiers() {
        writeFile(path + ".tiers", ::windows::tiersLine(getpid(), tiersSeq, tiersSet, bootSeconds()));
    }
    void writePrompt() {
        writeFile(path + ".prompt", ::windows::promptLine(getpid(), promptSeq, promptOpen, promptOutput, bootSeconds()));
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
    // SUPER+left-drag on the staged window (canvas mode, M7): the same codec as .pan.
    double dragX = 0, dragY = 0;
    bool dragStarted = false, dragActive = false;
    int fit = 0;
    bool canvasMode = false;
    std::optional<::windows::List> windows;    // a new list this update, else nullopt
    std::optional<::windows::Cursor> cursor;   // a new cursor line this update, else nullopt
    std::optional<::windows::Search> search;   // a new prompt line this update, else nullopt
    // The mode is announced with the next heartbeat; a live switch starts the canvas mailboxes over.
    void setCanvasMode(bool m) {
        heartbeat = 0;
        if (m == canvasMode) return;
        canvasMode = m;
        windows.reset(); cursor.reset(); search.reset(); tiersSet.clear(); tiersAt = -1e9;
        windowsSeq = cursorSeq = 0; windowsStamp = cursorStamp = searchStamp = {};
        drag = {}; dragX = dragY = 0; dragStarted = dragActive = false;
    }
    explicit LiveControls(const std::string& pose) : path(pose.empty() ? "" : pose + ".controls"), session(std::to_string(getpid())) {
        if (const char* mirrored = std::getenv("OMARCHY_XR_MIRROR_STATE")) mirror = mirrored;
        if (mirror == path) mirror.clear();
        if (!path.empty()) { unlink(path.c_str()); unlink((path + ".pan").c_str()); unlink((path + ".drag").c_str()); unlink((path + ".focus").c_str()); update(); }
    }
    ~LiveControls() {
        AsyncFile::instance().flush();
        if (path.empty()) return;
        for (const auto& base : {path, mirror}) {
            if (base.empty()) continue;
            unlink((base + ".pan").c_str()); unlink((base + ".pane").c_str()); unlink((base + ".active").c_str()); unlink(base.c_str());
            unlink((base + ".hover").c_str()); unlink((base + ".pointer").c_str());
            unlink((base + ".focus").c_str());unlink((base + ".notification").c_str());
            unlink((base + ".mode").c_str()); unlink((base + ".windows").c_str()); unlink((base + ".cursor").c_str());
            unlink((base + ".prompt").c_str()); unlink((base + ".fill").c_str()); unlink((base + ".search").c_str());
            unlink((base + ".tiers").c_str()); unlink((base + ".drag").c_str());
        }
    }
    // pointerSerial changes once per gaze dwell; pointerX/Y are that dwell's monitor pixel
    // coordinates (window-buffer pixels in canvas mode). The adapter warps the desktop pointer there
    // exactly once per serial. Canvas mode writes v4 (same fields, output = window address) to both files.
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
        writeFile(path + ".hover", (canvasMode ? "v4 " : "v3 ") + pointer.str());
        if (!mirror.empty()) writeFile(mirror + ".hover", canvasMode ? "v4 " + pointer.str() : values.str() + '\n');
    }
    void publishNotification(const std::string& identity) {
        if(path.empty())return;
        const auto now=bootSeconds();
        if(identity==notificationPublished && now==notificationStamp)return;
        notificationPublished=identity;notificationStamp=now;
        writeFile(path+".notification","v1 "+session+" "+hextoken::encodeHex(identity)+" "+std::to_string(now)+"\n");
    }
    static std::string decodeTarget(const std::string& token) { return hextoken::decodeHex(token); }
    // Opening or closing starts a new prompt sequence; the prompt's edit counter starts over with it.
    void publishPrompt(bool open, const std::string& output) {
        if (path.empty() || (open == promptOpen && output == promptOutput)) return;
        promptOpen = open; promptOutput = output; ++promptSeq; searchSeq = 0;
        writePrompt();
    }
    // Logical px of the canvas output for the staged window (Fill and its restore).
    void publishFill(const std::string& address, unsigned w, unsigned h) {
        const auto parsed = ::windows::parseAddress(address);
        if (path.empty() || !parsed) return;
        writeFile(path + ".fill", ::windows::fillLine(getpid(), ++fillSeq, *parsed, w, h, bootSeconds()));
    }
    // The live sliver set (§4.4, canvas mode): written when it changes, at most every 500 ms; a change
    // inside that window waits for a later call. The heartbeat keeps a non-empty set fresh.
    void publishTiers(std::vector<std::uint64_t> slivers, double nowMonotonic) {
        if (path.empty() || !canvasMode) return;
        std::sort(slivers.begin(), slivers.end());
        slivers.erase(std::unique(slivers.begin(), slivers.end()), slivers.end());
        if (slivers == tiersSet || nowMonotonic - tiersAt < .5) return;
        tiersSet = std::move(slivers); tiersAt = nowMonotonic; ++tiersSeq;
        writeTiers();
    }
    // The optional takeover chords (canvas.tsv takeoverKeys), announced with the next heartbeat.
    void setTakeover(bool on) { if (on != takeover) { takeover = on; heartbeat = 0; } }
    void update() {
        zoom = 0; fit = 0; focusOutput.clear();notificationTarget.clear(); windows.reset(); cursor.reset(); search.reset(); if (path.empty()) return;
        updateCumulative(".pan", pan, panX, panY, panStarted, panActive);
        if (canvasMode) updateCumulative(".drag", drag, dragX, dragY, dragStarted, dragActive);
        updatePane();
        updateFocus();
        updateWindows();
        updateCursor();
        updateSearch();
        beat();
        updateControls();
    }
    // Once per second: .active (owner + stamp), .mode (v1 owner canvas|monitors takeover stamp; the flag
    // switches the optional takeover chords, SUPER+F is always taken), an open .prompt and a non-empty
    // .tiers (same seq, fresh stamp), so a late reader still sees them and a dead renderer's go stale.
    void beat() {
        const auto now = bootSeconds();
        if (now == heartbeat) return;
        writeFile(path + ".active", session + ' ' + std::to_string(now) + '\n');
        writeFile(path + ".mode", "v1 " + session + " " + (canvasMode ? "canvas" : "monitors") + (takeover ? " 1 " : " 0 ") + std::to_string(now) + "\n");
        if (promptOpen) writePrompt();
        if (canvasMode && !tiersSet.empty()) writeTiers();
        if (!mirror.empty()) writeFile(mirror + ".active", session + ' ' + std::to_string(std::time(nullptr)) + '\n');
        heartbeat = now;
    }
    // Modes 0..7 everywhere, 8..18 (canvas verbs, §5.5; 18 confirm, M7) in canvas mode only; modes >= 6 need v3 and a
    // fresh stamp, and a target only for 6, 7 (notifications) and 14, 15 (direction tokens). 11 and 12
    // may carry the token "release" (the Alt-Tab release bind).
    void updateControls() {
        const auto filePath = existing("");
        if (filePath != controlsSeen) { controlsSeen = filePath; controlsStamp = {}; }
        if (!newer(filePath, controlsStamp)) return;
        std::ifstream file(filePath);
        std::string first, owner, extra; unsigned long long nextSerial, nextFit; double total; int mode;
        if (!(file >> first)) return;
        if (first == "v2" || first == "v3") { if (!(file >> owner)) return; }
        else owner = first;
        if (!(file >> nextSerial >> total >> nextFit >> mode) || owner != session ||
            !std::isfinite(total) || std::abs(total) > 1e9 || mode < 0 || mode > (canvasMode ? 18 : 7) || nextSerial <= serial) return;
        std::string target;long stamp=0;
        if(first=="v3") {
            std::string token;
            if(!(file>>token>>stamp))return;
            target=decodeTarget(token);
        }
        if(file>>extra)return;
        const bool needsTarget=mode==6 || mode==7 || mode==14 || mode==15;
        if(mode>=6 && (first!="v3" || (needsTarget && target.empty()) || !stampFresh(stamp)))return;
        if (nextFit != fitSerial) { fit = mode; fitSerial = nextFit;notificationTarget=target; }
        else zoom = std::clamp(total - previousZoom, -4., 4.);
        serial = nextSerial; previousZoom = total;
    }
};
