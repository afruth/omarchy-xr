// The real renderer's live scene switch (mode:canvas|monitors on the pose socket, plan §7 M6) and the
// notification HUD in a mono run (D6): hidden window, offline canvas, no compositor captures.
#define main rendererMain
#include "../src/main.cpp"
#undef main
#include <cassert>

namespace {
std::string readFile(const std::string& path) {
    std::ifstream in(path); return std::string((std::istreambuf_iterator<char>(in)), {});
}
void write(const std::string& path, const std::string& text) {
    const auto before=std::filesystem::exists(path) ? std::filesystem::last_write_time(path) : std::filesystem::file_time_type{};
    std::ofstream(path) << text;
    // A later mtime even on coarse clocks: the renderer polls by mtime only.
    std::filesystem::last_write_time(path, std::max(std::filesystem::last_write_time(path), before+std::chrono::seconds(1)));
}
long bootNow() { timespec boot{}; clock_gettime(CLOCK_BOOTTIME, &boot); return boot.tv_sec; }
// A .controls.windows list of `count` parked terminals (the first one staged) owned by this process.
std::string mailboxFile(unsigned long long seq, unsigned count) {
    std::ostringstream out;
    out << "v1 " << getpid() << ' ' << seq << ' ' << bootNow() << " 20000 0 OMXRTEST-canvas\n";
    for (unsigned i=0;i<count;++i)
        out << "0x" << std::hex << 0x6000+i << std::dec << ' ' << windows::encodeHex("foot") << ' ' << windows::encodeHex("term "+std::to_string(i))
            << " 1280 720 20000 0 " << i << ' ' << (i ? "park" : "stage") << " 0 " << 1000+i << " 0 1\n";
    return out.str();
}
// One datagram to the renderer's pose socket, as the backend sends it.
void send(const std::string& pose, const std::string& packet) {
    const int fd=socket(AF_UNIX, SOCK_DGRAM|SOCK_CLOEXEC, 0); assert(fd>=0);
    sockaddr_un address{}; address.sun_family=AF_UNIX; std::memcpy(address.sun_path, pose.c_str(), pose.size()+1);
    assert(sendto(fd, packet.data(), packet.size(), 0, reinterpret_cast<sockaddr*>(&address), sizeof(address))==ssize_t(packet.size()));
    close(fd);
}
std::string stats(View& v) {
    v.recordWork(1, monotonicSeconds()); AsyncFile::instance().flush();
    return readFile(v.posePath+".stats");
}
struct State {
    std::string temp, pose, viewer, canvasFile;
    explicit State(const std::string& dir) : temp(dir), pose(dir+"/pose.sock"), viewer(dir+"/viewer.tsv"), canvasFile(dir+"/canvas.tsv") {}
};
void prepare(View& v, SDL_Window* window, const State& s) {
    v.window=window; v.sceneBounds(); v.controls.emplace(s.pose);
    v.environment=std::make_unique<SkyEnvironment>("");
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &v.maxTexture);
    v.offlineCanvas=true; v.canvasPath=s.canvasFile;
}
void enteredCanvas(View& v, SkyEnvironment* sky) {
    assert(v.mode==View::SceneMode::Canvas && v.canvas && v.panels.empty() && v.geometry.empty() && v.controls->canvasMode);
    assert(v.distance==v.canvas->ring.radius && std::hypot(v.panX, v.panY, v.panZ)<=v.canvas->ring.radius-.3f+1e-3f);
    assert(v.environment.get()==sky && v.windowsSeq==0);
    v.controls->beat(); AsyncFile::instance().flush();
    assert(readFile(v.posePath+".controls.mode").find(" canvas ")!=std::string::npos);
    assert(stats(v).find("\"mode\":\"canvas\"")!=std::string::npos);
}
// (a) monitors -> canvas -> monitors -> canvas in one process: the datagram, the scene swap, the
// forced stats report, the mailbox in the new mode, the canvas memory and an empty monitor scene.
void roundTrip(SDL_Window* window, const State& s) {
    std::vector<Panel> panels(2);
    panels[0].layout={"OMXRTEST-left",0,0,1920,1080,40}; panels[1].layout={"OMXRTEST-right",1944,0,1920,1080,40};
    View v(panels, false, spatial::Workspace{40}, 24, "", s.pose, false, false, 64, 28, s.viewer, 60, false);
    prepare(v, window, s);
    assert(v.viewerPath==s.viewer);
    auto* sky=v.environment.get();
    send(s.pose, "mode:bogus"); v.tracking.update();
    assert(v.tracking.modeRequested.empty());
    send(s.pose, "mode:monitors"); v.tracking.update(); v.applyModeRequest();
    assert(v.mode==View::SceneMode::Monitors && v.panels.size()==2 && v.tracking.modeRequested.empty());
    send(s.pose, "mode:canvas"); v.tracking.update();
    assert(v.tracking.modeRequested=="canvas");
    v.applyModeRequest();
    assert(v.tracking.modeRequested.empty());
    enteredCanvas(v, sky);
    write(s.pose+".controls.windows", mailboxFile(1, 3)); v.steer();
    assert(v.canvas->live()==3 && v.canvas->stagedName=="0x6000");
    for (int frame=0;frame<5;++frame) assert(v.tick());
    assert(!std::filesystem::exists(s.temp+"/canvas-memory.tsv") && v.canvas->memory.dirty);
    // No OMXRTEST- output exists offline: the monitor scene stays empty until viewer.tsv changes.
    v.tracking.modeRequested="monitors"; v.applyModeRequest();
    assert(v.mode==View::SceneMode::Monitors && !v.canvas && v.panels.empty() && v.geometry.empty() && !v.controls->canvasMode);
    assert(v.environment.get()==sky && v.hoverOutput.empty() && v.selection.output.empty());
    AsyncFile::instance().flush();
    assert(std::filesystem::exists(s.temp+"/canvas-memory.tsv"));
    assert(stats(v).find("\"mode\":\"monitors\"")!=std::string::npos);
    for (int frame=0;frame<5;++frame) assert(v.tick());
    v.controls->beat(); AsyncFile::instance().flush();
    assert(readFile(s.pose+".controls.mode").find(" monitors ")!=std::string::npos);
    // A re-entered canvas starts clean and adopts the adapter's next list.
    v.tracking.modeRequested="canvas"; v.applyModeRequest();
    enteredCanvas(v, sky);
    assert(v.canvas->live()==0);
    write(s.pose+".controls.windows", mailboxFile(1, 2)); v.steer();  // seq 1 again: the mailbox started over
    assert(v.canvas->live()==2);
    assert(v.switchScene(View::SceneMode::Canvas) && v.mode==View::SceneMode::Canvas);
    for (int frame=0;frame<3;++frame) assert(v.tick());
    v.finishCanvas();
}
// (b) Smoke runs, developer window files, a missing canvas path and old XR controls keep the scene.
void refusals(const State& s, const std::string& oldControls) {
    std::vector<Panel> none; const std::string empty;
    const auto refused=[](View& v, View::SceneMode next) {
        const auto before=v.mode;
        assert(!v.switchScene(next) && v.mode==before && !v.canvas);
    };
    { View v(none, true, spatial::Workspace{}, 24, empty, s.pose, false, false, 64, 28, s.viewer, 60, false); v.canvasPath=s.canvasFile; v.offlineCanvas=true; refused(v, View::SceneMode::Canvas); }
    { View v(none, false, spatial::Workspace{}, 24, empty, s.pose, false, false, 64, 28, s.viewer, 60, false); v.canvasPath=s.canvasFile; v.offlineCanvas=true; v.windowsPath=s.temp+"/windows"; refused(v, View::SceneMode::Canvas); }
    { View v(none, false, spatial::Workspace{}, 24, empty, s.pose, false, false, 64, 28, s.viewer, 60, false); v.offlineCanvas=true; refused(v, View::SceneMode::Canvas); }
    // Not offline and no controls.version beside the pose socket: refused before any hub connect.
    { View v(none, false, spatial::Workspace{}, 24, empty, oldControls, false, false, 64, 28, s.viewer, 60, false); v.canvasPath=s.canvasFile; refused(v, View::SceneMode::Canvas); }
}
void publish(const std::filesystem::path& folder) {
    std::ofstream file(folder/"notifications.json.tmp");
    file << "{\"version\":1,\"generation\":\"mono\",\"time\":" << std::fixed << notifications::wallMilliseconds()
         << ",\"count\":1,\"palette\":{\"background\":\"#16242d\",\"text\":\"#d6e2ee\",\"accent\":\"#8bc9eb\"},"
            "\"entries\":[{\"key\":\"mono\",\"app\":\"Omarchy\",\"summary\":\"Mono HUD\",\"body\":\"Drawn without stereo.\"}]}";
    file.close(); std::filesystem::rename(folder/"notifications.json.tmp", folder/"notifications.json");
}
std::vector<unsigned char> frame(View& v) {
    v.renderScene(1280, 720, false, false, 1280, 720); glFinish();
    std::vector<unsigned char> pixels(1280*720*4);
    glReadPixels(0, 0, 1280, 720, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return pixels;
}
// (c) D6: a mono run with a pose socket builds the HUD and drawEye draws its cards.
void hudInMono(SDL_Window* window, const State& s) {
    std::vector<Panel> none; const std::string empty;
    { View stereoOnly(none, false, spatial::Workspace{}, 24, empty, empty, false, true, 64, 28, empty, 60, false); assert(!stereoOnly.wantsHud()); }
    View v(none, false, spatial::Workspace{}, 24, empty, s.pose, false, false, 64, 28, empty, 60, false);
    assert(v.wantsHud());
    v.window=window; v.sceneBounds(); v.primeCamera(); v.panZ=v.targetPanZ;
    v.environment=std::make_unique<SkyEnvironment>("");
    v.accent.path=s.temp+"/colors.toml"; v.accent.update(100); v.environment->update(100);
    publish(s.temp); v.notificationHud=std::make_unique<notifications::Hud>(std::filesystem::path(s.pose).parent_path().string());
    for (int i=0;i<100 && !v.notificationHud->visible();++i) { SDL_Delay(10); v.notificationHud->update(v.tracking.camera, 100); }
    assert(v.notificationHud->visible());
    for (int i=0;i<120;++i) {
        const double t=100+i/60.; v.lastCameraTime=t; v.tracking.camera.timestamp=t;
        v.notificationHud->update(v.tracking.camera, t); v.placeNotification(t);
    }
    assert(v.notificationHud->placement().onscreen);
    const auto withHud=frame(v);
    // A live switch to the canvas berths the card afresh inside the ring, not from monitor depth.
    v.controls.emplace(s.pose); v.canvasPath=s.canvasFile; v.offlineCanvas=true;
    assert(v.switchScene(View::SceneMode::Canvas));
    assert(v.notificationHud->placement().opacity==0 && v.notificationHud->placement().phase()==0);
    for (int i=0;i<60;++i) {
        const double t=102+i/60.; v.lastCameraTime=t; v.tracking.camera.timestamp=t;
        v.notificationHud->update(v.tracking.camera, t); v.placeNotification(t);
    }
    const auto& berth=v.notificationHud->placement();
    const notifications::space::Vec eye{-v.panX, -v.panY, -v.panZ};
    assert(notifications::space::length(targeting::sub(berth.position, eye))<=v.canvas->ring.radius-.3f+1e-3f);
    assert(v.switchScene(View::SceneMode::Monitors));
    v.notificationHud->release(); v.notificationHud.reset();
    const auto without=frame(v);
    assert(withHud!=without);
    v.environment->release(); v.halo.release();
}
}
int main() {
    assert(SDL_Init(SDL_INIT_VIDEO)==0);
    auto* window=SDL_CreateWindow("Mode switch", 0, 0, 1280, 720, SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN); assert(window);
    auto context=SDL_GL_CreateContext(window); assert(context);
    char temp[]="/tmp/xr-mode-switch-XXXXXX"; assert(mkdtemp(temp));
    const State s(temp);
    std::ofstream(s.viewer) << "# settings 60 40 24 -1 0\nOMXRTEST-missing\t0\t0\t1920\t1080\t40\t100\n";
    std::ofstream(std::string(temp)+"/controls.version") << "6\n";
    const auto old=std::string(temp)+"/old";
    std::filesystem::create_directory(old);
    roundTrip(window, s);
    refusals(s, old+"/pose.sock");
    hudInMono(window, s);
    AsyncFile::instance().flush(); std::filesystem::remove_all(temp);
    SDL_GL_DeleteContext(context); SDL_DestroyWindow(window); SDL_Quit();
    std::cout << "Mode switch: the mode: datagram, monitors -> canvas -> monitors -> canvas in one process, the forced stats report, the .mode heartbeat, a clean mailbox after re-entry, canvas memory on exit, an empty monitor scene without captures, the refusals and the HUD in a mono run passed\n";
}
