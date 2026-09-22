#include "notification_hud.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <SDL.h>
#include <unistd.h>

using namespace notifications;
std::string packet(double stamp,const std::string& body="Hello &amp; welcome") {
    return "{\"version\":1,\"generation\":\"session\",\"time\":"+std::to_string(stamp)+
        ",\"count\":2,\"entries\":[{\"key\":\"123:1\",\"app\":\"Chat\",\"summary\":\"Notification\",\"body\":\""+body+
        "\",\"urgency\":1}],\"palette\":{\"background\":\"#16242d\",\"text\":\"#d6e2ee\",\"accent\":\"#8bc9eb\"}}";
}
void content() {
    const auto card=readCard(packet(10000),10001);assert(card && card->count==2 && card->key=="123:1");
    assert(!readCard(packet(10000),14000) && !readCard(packet(10000),9000));
    assert(!readCard("{",10000) && !readCard("null",10000) && !readCard("[]",10000));
    assert(!readCard(std::string(300000,'x'),10000));
    assert(plainBody("<b>Hello</b><br>世界 &amp; &#x1f44b;<img src='https://example.test/x'>")=="Hello\n世界 & 👋");
    assert(plainBody("<im<img src='x'>g>")=="g>");
    const auto encoded=parse(dismissal(*card,10020));assert(encoded);
    assert(string(encoded.get(),"key",128)=="123:1" && string(encoded.get(),"generation",128)=="session");
    const auto image=rasterize(*card);assert(image->width==720 && image->height>80 && image->height<=310);
    const auto tinted=color("#80112233",{});assert(std::abs(tinted[3]-128./255)<1e-9);
}
struct Motion {
    HeadShake detector;double now=10;int dismissals=0;
    void sample(double yaw,double pitch=0,double roll=0,bool fresh=true,const std::string& id="alert") {
        now+=.01;if(detector.update(id,yaw,pitch,roll,now,fresh)) ++dismissals;
    }
    void rest(double yaw=0) {for(int i=0;i<60;i++)sample(yaw);}
    void move(double from,double to,int steps=20,double pitch=0,double roll=0) {
        for(int i=1;i<=steps;i++)sample(from+(to-from)*i/steps,pitch,roll);
    }
    void shake(double centre=0,double sign=1) {move(centre,centre+13*sign);move(centre+13*sign,centre-13*sign,35);move(centre-13*sign,centre,20);}
};
void gestures() {
    for(double centre:{0.,179.,-179.})for(double sign:{-1.,1.}) {
        Motion m;m.rest(centre);m.shake(centre,sign);assert(m.dismissals==1);
        m.shake(centre,sign);assert(m.dismissals==1); // one continuous shake never clears the queue
    }
    Motion glance;glance.rest();glance.move(0,25);glance.move(25,0);assert(glance.dismissals==0);
    Motion slow;slow.rest();slow.move(0,13,100);slow.move(13,-13,200);slow.move(-13,0,100);assert(slow.dismissals==0);
    Motion nod;nod.rest();for(int i=0;i<150;i++)nod.sample(0,15*std::sin(i*.08));assert(nod.dismissals==0);
    Motion drift;drift.rest();for(int i=0;i<500;i++)drift.sample(i*.03);assert(drift.dismissals==0);
    Motion jitter;jitter.rest();for(int i=0;i<500;i++)jitter.sample(2*std::sin(i));assert(jitter.dismissals==0);
    Motion stale;stale.rest();stale.move(0,13);stale.sample(-13,0,0,false);stale.move(-13,0);assert(stale.dismissals==0);
    Motion startup;startup.shake();assert(startup.dismissals==0);
    Motion none;none.rest();for(int i=0;i<150;i++)none.sample(13*std::sin(i*.08),0,0,true,"");assert(none.dismissals==0);
    Motion changed;changed.rest();changed.move(0,13);changed.sample(-13,0,0,true,"new-alert");changed.move(-13,0);assert(changed.dismissals==0);
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
        assert(overlay.visible() && overlay.count()==2);
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
            auto card=readCard(packet(wallMilliseconds()),wallMilliseconds());assert(card);auto bitmap=rasterize(*card);
            auto* image=cairo_image_surface_create_for_data(bitmap->pixels.data(),CAIRO_FORMAT_ARGB32,bitmap->width,bitmap->height,bitmap->width*4);
            assert(cairo_surface_write_to_png(image,capture)==CAIRO_STATUS_SUCCESS);cairo_surface_destroy(image);
        }
        assert(glGetError()==GL_NO_ERROR);
        overlay.place(scene,12);
        double time=20;
        auto sample=[&](double yaw) {time+=.01;camera.timestamp=time;camera.axes[2].raw=yaw;overlay.update(camera,time);};
        for(int i=0;i<60;i++)sample(0);
        for(int i=1;i<=20;i++)sample(13.*i/20);
        for(int i=1;i<=35;i++)sample(13.-26.*i/35);
        for(int i=1;i<=20;i++)sample(-13.+13.*i/20);
        assert(!overlay.visible());AsyncFile::instance().flush();
        auto request=parse(readFile(std::string(temp)+"/notification-dismiss.json"));
        assert(request && string(request.get(),"key",128)=="123:1");
        std::filesystem::remove(std::string(temp)+"/notifications.json");
        for(int i=0;i<100 && overlay.visible();i++){SDL_Delay(10);overlay.update(camera,12);}
        assert(!overlay.visible());overlay.release();
    }
    std::filesystem::remove_all(temp);SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();
}
int main() {content();gestures();hud();std::cout<<"Notification content, shake rejection/recognition, 3D rendering and expiry passed\n";}
