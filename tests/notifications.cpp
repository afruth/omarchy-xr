#include "notification_hud.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <SDL.h>
#include <unistd.h>

using namespace notifications;
std::string packet(double stamp,const std::string& body="Hello &amp; welcome") {
    return "{\"version\":1,\"generation\":\"session\",\"time\":"+std::to_string(stamp)+
        ",\"count\":4,\"entries\":[{\"key\":\"123:1\",\"app\":\"Chat\",\"summary\":\"Notification\",\"body\":\""+body+
        "\",\"urgency\":1},{\"key\":\"123:2\",\"app\":\"Build\",\"summary\":\"Tests passed\"},{\"key\":\"123:3\",\"app\":\"Mail\",\"summary\":\"New message\"},{\"key\":\"123:4\",\"app\":\"Download\",\"summary\":\"Complete\"}],\"palette\":{\"background\":\"#16242d\",\"text\":\"#d6e2ee\",\"accent\":\"#8bc9eb\"}}";
}
void content() {
    const auto cards=readCards(packet(10000),10001);assert(cards.size()==4 && cards[0].key=="123:1");
    const auto& card=cards[0];
    assert(readCards(packet(10000),14000).empty() && readCards(packet(10000),9000).empty());
    assert(readCards("{",10000).empty() && readCards("null",10000).empty() && readCards("[]",10000).empty());
    assert(readCards(std::string(1100000,'x'),10000).empty());
    assert(plainBody("<b>Hello</b><br>世界 &amp; &#x1f44b;<img src='https://example.test/x'>")=="Hello\n世界 & 👋");
    assert(plainBody("<im<img src='x'>g>")=="g>");
    const auto encoded=parse(dismissal(card,10020));assert(encoded);
    assert(string(encoded.get(),"key",128)=="123:1" && string(encoded.get(),"generation",128)=="session");
    const auto image=rasterize(card);assert(image->width==720 && image->height>80 && image->height<=310);
    const auto tinted=color("#80112233",{});assert(std::abs(tinted[3]-128./255)<1e-9);
}
void aim(Hud& overlay,space::Scene& scene,tracking::Camera& camera,double now,size_t layer=0) {
    const auto d=space::normalize(space::sub(overlay.position(layer),scene.eye));
    scene.view=tracking::conjugate(tracking::orientation(0,-std::asin(d.y)*180/spatial::pi,-std::atan2(d.x,-d.z)*180/spatial::pi));
    camera.timestamp=now;overlay.update(camera,now);overlay.place(scene,now);
}
void gestures(Hud& overlay,space::Scene scene,tracking::Camera& camera,const std::string& directory) {
    assert(overlay.highlight().empty());
    const auto first=overlay.front();
    assert(!overlay.flick(first,true) && !overlay.flick(first,false));
    aim(overlay,scene,camera,12);
    assert(overlay.highlight()==first);
    assert(!overlay.flick("foreign",true));
    aim(overlay,scene,camera,12,1);
    assert(overlay.highlight()==readCards(packet(10000),10001)[1].identity());
    aim(overlay,scene,camera,12);
    for(int i=0;i<4;++i) {
        aim(overlay,scene,camera,12+i*.02);
        const auto previous=overlay.front();assert(overlay.flick(overlay.highlight(),false));
        assert(overlay.count()==4 && overlay.front()!=previous);
    }
    assert(overlay.front()==first);
    assert(!std::filesystem::exists(directory+"/notification-dismiss.json"));
    aim(overlay,scene,camera,12.1);
    assert(overlay.flick(first,true) && overlay.count()==3);
    AsyncFile::instance().flush();
    auto request=parse(readFile(directory+"/notification-dismiss.json"));
    assert(request && string(request.get(),"key",128)=="123:1");
    assert(!overlay.flick(first,true)); // one action cannot clear a second card
    // A heartbeat with updated content must not resurrect the dismissed card or reset cycling.
    {std::ofstream file(directory+"/notifications.json");file<<packet(wallMilliseconds(),"Updated");}
    SDL_Delay(150);camera.timestamp=12.2;overlay.update(camera,12.2);assert(overlay.count()==3);
    aim(overlay,scene,camera,12.3);assert(!overlay.highlight().empty());
    overlay.update(camera,13);assert(overlay.highlight().empty()); // stale tracking
    assert(!overlay.flick(overlay.front(),true));
    aim(overlay,scene,camera,13.1);
    scene.view=tracking::conjugate(tracking::orientation(0,0,180));overlay.place(scene,13.1);
    assert(overlay.highlight().empty() && !overlay.flick(overlay.front(),true));
    // Head motion alone never dismisses anything.
    for(int i=0;i<150;++i) {
        camera.timestamp=14+i*.01;camera.axes[1].raw=15*std::sin(i*.08);camera.axes[2].raw=15*std::sin(i*.12);
        overlay.update(camera,camera.timestamp);
    }
    assert(overlay.count()==3);
    // A desktop dismissal removes only that card from an otherwise live feed.
    auto desktop=parse(packet(wallMilliseconds()));
    json_object_array_del_idx(field(desktop.get(),"entries"),1,1);
    {std::ofstream file(directory+"/notifications.json");file<<json_object_to_json_string(desktop.get());}
    for(int i=0;i<100 && overlay.count()!=2;++i){SDL_Delay(10);overlay.update(camera,16);}
    assert(overlay.count()==2);
}
void hud() {
    assert(SDL_Init(SDL_INIT_VIDEO)==0);
    auto* window=SDL_CreateWindow("Notification test",0,0,1280,720,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);assert(window);
    auto context=SDL_GL_CreateContext(window);assert(context);
    char temp[]="/tmp/xr-notification-test-XXXXXX";assert(mkdtemp(temp));
    {
        Hud overlay(temp);tracking::Camera camera;
        {std::ofstream file(std::string(temp)+"/notifications.json");file<<packet(wallMilliseconds());}
        for(int i=0;i<100 && !overlay.visible();i++){SDL_Delay(10);overlay.update(camera,10);}
        assert(overlay.visible() && overlay.count()==4);
        space::Scene scene;scene.tanV=.4f;scene.tanH=.4f*1280/720;
        overlay.place(scene,10);overlay.place(scene,11);
        auto projection=[&](float eye=0){
            glMatrixMode(GL_PROJECTION);glLoadIdentity();glFrustum(-scene.tanH*.1,scene.tanH*.1,-scene.tanV*.1,scene.tanV*.1,.1,100);
            glMatrixMode(GL_MODELVIEW);glLoadIdentity();glTranslatef(-eye,0,0);
        };
        glViewport(0,0,1280,720);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glMatrixMode(GL_MODELVIEW);glLoadIdentity();projection();
        overlay.draw(11);
        assert(glIsEnabled(GL_DEPTH_TEST));GLboolean depth;glGetBooleanv(GL_DEPTH_WRITEMASK,&depth);assert(depth);
        GLfloat matrix[16];glGetFloatv(GL_MODELVIEW_MATRIX,matrix);assert(matrix[12]==0 && matrix[13]==0);
        std::vector<unsigned char> pixels(1280*720*4);glReadPixels(0,0,1280,720,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        size_t lit=0;for(size_t i=0;i<pixels.size();i+=4)if(pixels[i]>2)++lit;
        assert(lit>1000);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glTranslatef(-1,0,0);overlay.draw(11);
        std::vector<unsigned char> moved(pixels.size());glReadPixels(0,0,1280,720,GL_RGBA,GL_UNSIGNED_BYTE,moved.data());
        assert(moved!=pixels); // it lives in the environment and moves with scene projection
        auto renderEye=[&](float eye) {
            projection(eye);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);overlay.draw(11);
            std::vector<unsigned char> image(pixels.size());glReadPixels(0,0,1280,720,GL_RGBA,GL_UNSIGNED_BYTE,image.data());return image;
        };
        auto centroid=[](const std::vector<unsigned char>& image){
            double sum=0,count=0;for(int y=0;y<720;++y)for(int x=0;x<1280;++x)if(image[(y*1280+x)*4]>2){sum+=x;++count;}
            assert(count>0);return sum/count;
        };
        assert(centroid(renderEye(-.032f))-centroid(renderEye(.032f))>5); // real stereo depth
        projection();glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glDisable(GL_TEXTURE_2D);glColor3f(.3f,.4f,.5f);glBegin(GL_QUADS);
        glVertex3f(-3,-2,-2);glVertex3f(3,-2,-2);glVertex3f(3,2,-2);glVertex3f(-3,2,-2);glEnd();
        std::vector<unsigned char> screen(pixels.size()),occluded(pixels.size());
        glReadPixels(0,0,1280,720,GL_RGBA,GL_UNSIGNED_BYTE,screen.data());overlay.draw(11);
        glReadPixels(0,0,1280,720,GL_RGBA,GL_UNSIGNED_BYTE,occluded.data());
        assert(screen==occluded); // monitor pixels remain untouched when the card flies behind
        overlay.release();assert(!overlay.visible());overlay.update(camera,11);
        assert(overlay.visible()); // cached content re-uploads after a lease/context replacement
        const char* capture=std::getenv("XR_NOTIFICATION_CAPTURE");
        if(capture) {
            auto cards=readCards(packet(wallMilliseconds()),wallMilliseconds());assert(!cards.empty());auto bitmap=rasterize(cards[0]);
            auto* image=cairo_image_surface_create_for_data(bitmap->pixels.data(),CAIRO_FORMAT_ARGB32,bitmap->width,bitmap->height,bitmap->width*4);
            assert(cairo_surface_write_to_png(image,capture)==CAIRO_STATUS_SUCCESS);cairo_surface_destroy(image);
        }
        assert(glGetError()==GL_NO_ERROR);
        gestures(overlay,scene,camera,temp);
        std::filesystem::remove(std::string(temp)+"/notifications.json");
        for(int i=0;i<100 && overlay.visible();i++){SDL_Delay(10);overlay.update(camera,12);}
        assert(!overlay.visible());overlay.release();
    }
    std::filesystem::remove_all(temp);SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();
}
int main() {content();hud();std::cout<<"Notification stack, gaze, flick dismissal/cycling, 3D rendering and expiry passed\n";}
