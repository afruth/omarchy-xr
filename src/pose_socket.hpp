#pragma once
#include "tracking.hpp"
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <ctime>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

inline double monotonicSeconds() {
    timespec t{}; clock_gettime(CLOCK_MONOTONIC,&t);
    return t.tv_sec+t.tv_nsec/1e9;
}
class PoseSocket {
    int fd=-1;
    std::string path;
public:
    tracking::Camera camera;
    bool recenterRequested=false,fitRequested=false,fitTargetRequested=false;
    int zoom=0,spectator=-1;
    // Canvas verbs (Studio, scripts; docs/infinite-canvas-plan.md §5.5): the View reads them in canvas
    // mode only and clears them otherwise. focus:0x<hex> lands on and focuses that window.
    bool overviewRequested=false,searchRequested=false,fillRequested=false,arrangeRequested=false,undoRequested=false,
        redoRequested=false,pinRequested=false,helpRequested=false;
    std::string focusRequested;
    bool canvasVerb(const std::string& packet) {
        static constexpr std::pair<const char*,bool PoseSocket::*> verbs[]={{"overview",&PoseSocket::overviewRequested},
            {"search",&PoseSocket::searchRequested},{"fill",&PoseSocket::fillRequested},{"arrange",&PoseSocket::arrangeRequested},
            {"undo",&PoseSocket::undoRequested},{"redo",&PoseSocket::redoRequested},{"pin",&PoseSocket::pinRequested},{"help",&PoseSocket::helpRequested}};
        for(const auto& [name,flag]:verbs) if(packet==name) { this->*flag=true; return true; }
        if(!packet.starts_with("focus:0x") || packet.size()>6+18) return false;
        focusRequested=packet.substr(6);
        return true;
    }
    void clearCanvasVerbs() {
        overviewRequested=searchRequested=fillRequested=arrangeRequested=undoRequested=redoRequested=pinRequested=helpRequested=false;
        focusRequested.clear();
    }
    explicit PoseSocket(const std::string& name):path(name) {
        if(path.empty()) return;
        sockaddr_un address{}; address.sun_family=AF_UNIX;
        if(path.size()>=sizeof(address.sun_path)) throw std::runtime_error("Pose socket path is too long");
        struct stat st{};
        if(lstat(path.c_str(),&st)==0) {
            if(!S_ISSOCK(st.st_mode) || st.st_uid!=getuid()) throw std::runtime_error("Refusing to replace unrelated pose path");
            unlink(path.c_str());
        }
        std::memcpy(address.sun_path,path.c_str(),path.size()+1);
        fd=socket(AF_UNIX,SOCK_DGRAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
        if(fd<0 || bind(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))<0) {
            if(fd>=0) close(fd);
            throw std::runtime_error("Cannot open pose socket: "+path);
        }
        chmod(path.c_str(),0600);
    }
    PoseSocket(const PoseSocket&)=delete;
    ~PoseSocket() { if(fd>=0) {close(fd); unlink(path.c_str());} }
    void update() {
        if(fd<0) return;
        char data[256];
        for(int i=0;i<256;++i) {
            auto n=recv(fd,data,sizeof(data),MSG_DONTWAIT|MSG_TRUNC);
            if(n<0) break;
            if(n<=ssize_t(sizeof(data))) {
                const std::string packet(data,n);
                if(packet=="spectator_on")spectator=1;
                else if(packet=="spectator_off")spectator=0;
                else if(packet=="recenter")recenterRequested=true;
                else if(packet=="fit")fitRequested=true;
                else if(packet=="fit_target" || packet=="fit_center")fitTargetRequested=true;
                else if(packet=="zoom_in")++zoom;
                else if(packet=="zoom_out")--zoom;
                else if(canvasVerb(packet)) {}
                else camera.accept(packet,monotonicSeconds());
            }
        }
    }
};
