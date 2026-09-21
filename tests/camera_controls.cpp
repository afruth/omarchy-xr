#include "camera_controls.hpp"
#include "live_controls.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <optional>

int main() {
    float at30=10,at120=10;
    for(int i=0;i<30;++i)at30=navigation::easeDistance(at30,2,1.f/30);
    for(int i=0;i<120;++i)at120=navigation::easeDistance(at120,2,1.f/120);
    assert(std::abs(at30-at120)<1e-5f && at30>2 && at30<2.002f);
    assert(navigation::easeDistance(2,10,.016f)>2 && navigation::easeDistance(2,10,.016f)<10);
    const float d=navigation::heightDistance(1080,28);
    assert(std::abs(2*d*std::tan(28*spatial::pi/360)-1.2f*1.04f)<1e-5f);
    std::vector<PanelLayout> panels{{"left",0,0,1920,1080},{"center",1944,0,1920,1080},{"right",3888,0,1920,1080}};
    assert(navigation::centerPanel(panels,2904,540)==1);
    assert(!navigation::fits(panels,2904,540,5808.f/900,1,80,28,16.f/9));
    assert(navigation::fits(panels,2904,540,5808.f/900,30,80,28,16.f/9));
    panels[1].curvature=100;
    assert(navigation::fits(panels,2904,540,5808.f/900,30,80,28,16.f/9));
    // Workspace-curved side monitors become front-facing without changing their mesh.
    for(float workspace:{0.f,50.f,100.f})for(float bend:{0.f,100.f})for(float x:{-3.f,0.f,3.f}){
        PanelLayout p{"focus",0,0,1920,1080,bend};
        const auto pose=spatial::pose(x,.7f,p.width/900,8,5,workspace,bend);
        const auto anchor=tracking::conjugate(tracking::orientation(12,25,-40));
        const auto depth=navigation::frontHeightDistance(p,pose,28);
        for(float d:{depth,depth*2}){
            const auto camera=navigation::frontFocus(pose,anchor,d);
            assert(std::abs(camera.rotation.x)<1e-8 && std::abs(camera.rotation.z)<1e-8);
            const auto view=tracking::multiply(navigation::headingOnly(anchor),camera.rotation);
            auto halfway=navigation::easeRotation({},camera.rotation,.016f);
            const auto up=targeting::rotate(halfway,{0,1,0});
            assert(std::abs(up.x)<1e-6 && std::abs(up.y-1)<1e-6 && std::abs(up.z)<1e-6);
            const auto c=targeting::rotate(view,targeting::add(pose.center,camera.pan));
            assert(std::abs(c.x)<1e-5 && std::abs(c.y)<1e-5 && std::abs(c.z+d)<1e-5);
            const auto normal=targeting::rotate(view,{std::sin(pose.yaw),0,std::cos(pose.yaw)});
            assert(std::abs(normal.x)<1e-5 && std::abs(normal.y)<1e-5 && normal.z>.9999);
            for(int j=0;j<=100;++j)for(float y:{-p.height/1800,p.height/1800}){
                const auto v=targeting::rotate(view,targeting::add(spatial::vertex(pose,(float(j)/100-.5f)*p.width/900,y),camera.pan));
                assert(v.z<0 && std::abs(v.y)/-v.z<std::tan(28*spatial::pi/360));
            }
        }
    }
    // Tilt at selection must not change the eventual navigation orientation.
    spatial::Pose side{{2,1,-4},.7f,.2f};
    auto level=navigation::frontFocus(side,tracking::conjugate(tracking::orientation(0,0,40)),3);
    for(float roll:{-45.f,0.f,45.f})for(float pitch:{-60.f,0.f,60.f}){
        auto tilted=navigation::frontFocus(side,tracking::conjugate(tracking::orientation(roll,pitch,40)),3);
        assert(std::abs(tilted.rotation.w-level.rotation.w)<1e-6);
        assert(std::abs(tilted.rotation.y-level.rotation.y)<1e-6);
        assert(tilted.rotation.x==0 && tilted.rotation.z==0);
    }
    // Recentering selected curved side panels preserves camera-to-panel distance.
    for(float depth:{.5f,2.f,8.f})for(float bend:{0.f,80.f}){
        const auto pose=spatial::pose(3,1,2,8,5,bend,0);
        auto before=navigation::frontFocus(pose,tracking::orientation(0,0,35),depth);
        auto after=navigation::frontFocus(pose,{},navigation::viewingDistance(pose,before.pan));
        auto center=targeting::rotate(after.rotation,targeting::add(pose.center,after.pan));
        assert(std::abs(center.x)<1e-5 && std::abs(center.y)<1e-5 && std::abs(center.z+depth)<1e-5);
    }
    // Lateral offsets used to inflate radial distance on recenter.
    spatial::Pose flat{{0,0,-5},0,0};
    assert(std::abs(navigation::viewingDistance(flat,{80,40,3})-2)<1e-6);
    // Reproduce a narrow-gutter curved layout whose safe geometry is very far away.
    std::vector<PanelLayout> tight{{"top",1804,-320,1024,200,68.494f},
        {"portrait",5274,0,720,1440},{"wide",1804,0,3440,1440},
        {"top2",2858,-320,1024,200}};
    const float far=spatial::safeDistance(tight,3899,560,4190.f/900,5.88181f,100,30);
    assert(far>100);
    const float overview=navigation::overviewDepth(tight,3899,560,4190.f/900,far,100,28,16.f/9);
    assert(overview>1 && overview<20);
    const auto farPose=spatial::pose(0,0,2,5,far,100,0);
    const auto close=navigation::frontFocus(farPose,{},2);
    for(int i=0;i<100;++i){
        const float depth=navigation::viewingDistance(farPose,close.pan);
        assert(std::abs(depth-2)<.001);
        assert(navigation::zoomDepth(depth,-100,overview*2)==overview*2);
        assert(navigation::zoomDepth(depth,100,overview*2)==.3f);
    }
    float limited=2;
    for(int i=0;i<1000;++i)limited=navigation::zoomDepth(limited,-.1f,overview*2);
    assert(limited==overview*2);
    assert(navigation::zoomDepth(limited,.1f,overview*2)<limited);
    spatial::Workspace cylinder;cylinder.degrees=180;cylinder.follow=true;
    auto cylinderPose=spatial::pose(2,1,2,8,5,cylinder,0);
    const auto camera=navigation::frontFocus(cylinderPose,{},3);
    auto centered=targeting::rotate(camera.rotation,targeting::add(cylinderPose.center,camera.pan));
    assert(std::abs(centered.x)<1e-5 && std::abs(centered.y)<1e-5 && std::abs(centered.z+3)<1e-5);
    auto hit=targeting::intersect(targeting::viewRay(camera.rotation,camera.pan),PanelLayout{"cylinder",0,0,1800,1080},cylinderPose);
    assert(hit && std::abs(hit->u-.5f)<.001 && std::abs(hit->v-.5f)<.001);
    for(bool follows:{false,true})for(float bend:{0.f,70.f}){
        spatial::Workspace wrap;wrap.degrees=100;wrap.follow=follows;
        PanelLayout panel{"pan",0,0,3440,1440,bend};
        auto pose=spatial::pose(2,1,panel.width/900,8,5,wrap,bend);
        auto limits=navigation::panLimits(panel,pose,1,28,16.f/9);
        assert(limits.x>0 && limits.y>0);
        for(int i=0;i<=100;++i){
            const float x=limits.x*(float(i)/50-1),y=limits.y*(float(i)/50-1);
            auto camera=navigation::panFocus(pose,{},1,x,y);
            auto point=targeting::rotate(camera.rotation,targeting::add(spatial::vertex(pose,x,y),camera.pan));
            assert(std::abs(point.x)<1e-5 && std::abs(point.y)<1e-5 && std::abs(point.z+1)<1e-5);
        }
        auto overview=navigation::panLimits(panel,pose,50,28,16.f/9);
        assert(overview.x==0 && overview.y==0);
    }
    spatial::Pose plane{{0,0,-5},0,0};
    auto edge=navigation::panLimits(PanelLayout{"p",0,0,1920,1080},plane,1,28,16.f/9);
    assert(std::abs(edge.x+std::tan(14*spatial::pi/180)*16/9-1920.f/1800-64.f/900)<1e-5);
    // Offset gaze zoom is not the monitor center: panLimits is 0 while the
    // panel still fits, and that clamp must not eat an on-panel origin.
    PanelLayout gazed{"gaze",0,0,1920,1080};
    const auto gazePose=spatial::pose(0,0,gazed.width/900,2,5,0,0);
    targeting::Hit look;look.output="gaze";look.u=.75f;look.v=.25f;
    const auto origin=navigation::gazeFocus(gazed,look);
    assert(origin.x>0 && origin.y>0);
    const float fitDepth=navigation::frontHeightDistance(gazed,gazePose,28);
    for(float depth:{5.f,fitDepth}){
        auto limits=navigation::panLimits(gazed,gazePose,depth,28,16.f/9);
        assert(limits.x==0 && limits.y==0);
        auto centered=navigation::applyPanLimits(gazed,gazePose,depth,28,16.f/9,origin,false);
        assert(centered.x==0 && centered.y==0);
        auto kept=navigation::applyPanLimits(gazed,gazePose,depth,28,16.f/9,origin,true);
        assert(std::abs(kept.x-origin.x)<1e-6 && std::abs(kept.y-origin.y)<1e-6);
        const float zoomedDepth=navigation::zoomDepth(depth,-std::log(.9f),20);
        auto towardLook=navigation::panFocus(gazePose,{},zoomedDepth,kept.x,kept.y);
        auto towardCenter=navigation::panFocus(gazePose,{},zoomedDepth,0,0);
        const auto lookPoint=spatial::vertex(gazePose,origin.x,origin.y);
        auto onAxis=targeting::rotate(towardLook.rotation,targeting::add(lookPoint,towardLook.pan));
        auto offAxis=targeting::rotate(towardCenter.rotation,targeting::add(lookPoint,towardCenter.pan));
        auto centerFromLook=targeting::rotate(towardLook.rotation,targeting::add(gazePose.center,towardLook.pan));
        assert(std::abs(onAxis.x)<1e-5 && std::abs(onAxis.y)<1e-5 && std::abs(onAxis.z+zoomedDepth)<1e-5);
        assert(std::abs(offAxis.x)>1e-3);
        assert(std::abs(centerFromLook.x)>1e-3);
    }
    // A pitched look's heading-only zoom walks a fresh sample toward the top
    // edge; the gesture must keep the first UV across ticks.
    const float pitchDeg=-std::atan2(origin.y,5.f)*180/spatial::pi;
    const float yawDeg=std::atan2(origin.x,5.f)*180/spatial::pi;
    auto pitched=targeting::viewRotation({},pitchDeg,yawDeg);
    auto first=targeting::intersect(targeting::viewRay(pitched,{}),gazed,gazePose);
    assert(first && std::abs(first->u-.75f)<.01f && std::abs(first->v-.25f)<.01f);
    std::optional<targeting::Hit> locked;
    assert(navigation::lockZoomGaze(locked,first));
    float depth=5;
    std::optional<targeting::Hit> sample=first;
    for(int tick=0;tick<5;++tick){
        assert(navigation::lockZoomGaze(locked,sample));
        assert(std::abs(locked->u-first->u)<1e-5 && std::abs(locked->v-first->v)<1e-5);
        auto focus=navigation::applyPanLimits(gazed,gazePose,depth,28,16.f/9,navigation::gazeFocus(gazed,*locked),true);
        depth=navigation::zoomDepth(depth,-std::log(.9f),20);
        auto camera=navigation::panFocus(gazePose,pitched,depth,focus.x,focus.y);
        auto view=tracking::multiply(pitched,camera.rotation);
        sample=targeting::intersect(targeting::viewRay(view,camera.pan),gazed,gazePose);
    }
    assert(sample && std::abs(sample->v-first->v)>1e-3);
    assert(std::abs(locked->u-first->u)<1e-5 && std::abs(locked->v-first->v)<1e-5);
    // Gaze must see empty space through the retained gutter, not a stretched panel.
    spatial::Workspace gutterWrap;gutterWrap.follow=true;gutterWrap.degrees=90;gutterWrap.gap=30.f/900;
    std::vector<PanelLayout> neighbours{{"a",0,0,1920,1080},{"b",1950,0,1920,1080}};
    auto gapPose=spatial::pose(0,0,1,3870.f/900,5,gutterWrap,0);
    targeting::Vec eye{0,0,1/gapPose.surfaceBend-5};
    auto gapRay=targeting::Ray{eye,targeting::sub(gapPose.center,eye)};
    assert(!targeting::query(gapRay,neighbours,1935,540,3870.f/900,5,gutterWrap));
    // Calibration changes heading instantly, but rebasing keeps the rendered
    // orientation continuous; only the subsequent animation moves the scene.
    for(float yaw:{-170.f,0.f,170.f})for(float pitch:{-45.f,0.f,45.f}){
        auto before=tracking::conjugate(tracking::orientation(10,pitch,yaw));
        auto after=tracking::conjugate(tracking::orientation(10,pitch,0));
        auto nav=tracking::orientation(0,0,35);
        auto oldView=tracking::multiply(before,nav);
        auto rebased=navigation::preserveView(oldView,after);
        for(auto v:{targeting::Vec{1,0,0},targeting::Vec{0,1,0},targeting::Vec{0,0,-1}}){
            auto a=targeting::rotate(oldView,v),b=targeting::rotate(tracking::multiply(after,rebased),v);
            assert(std::abs(a.x-b.x)<1e-5 && std::abs(a.y-b.y)<1e-5 && std::abs(a.z-b.z)<1e-5);
        }
        auto animated=rebased;
        for(int i=0;i<60;++i)animated=navigation::easeRotation(animated,{},1.f/120);
        assert(std::abs(animated.w)> .999 && std::abs(animated.y)<.02);
    }
    char temp[]="/tmp/xr-input-test-XXXXXX";assert(mkdtemp(temp));
    const std::string pose=std::string(temp)+"/pose.sock",path=pose+".controls";
    {
        LiveControls input(pose);
        auto write=[&](std::string packet){std::ofstream file(path);file<<packet;};
        const auto owner=std::to_string(getpid());
        write(owner+" 1 0.2 0 0");input.update();assert(std::abs(input.zoom-.2)<1e-8);
        input.update();assert(input.zoom==0); // duplicated frames never replay motion
        write(owner+" 7 0.8 0 0");input.update();assert(std::abs(input.zoom-.6)<1e-8); // coalescing
        write(owner+" 8 0.8 8 2");input.update();assert(input.fit==2 && input.zoom==0);
        write(owner+" 9 0.4 8 2");input.update();assert(std::abs(input.zoom+.4)<1e-8);
        write("other-session 10 4 10 1");input.update();assert(input.fit==0 && input.zoom==0);
        write(owner+" 10 nan 10 1");input.update();assert(input.fit==0 && input.zoom==0);
        write(owner+" 11 0.4 11 1");input.update();assert(input.fit==1);
        write(owner+" 12 0.4 12 3");input.update();assert(input.fit==3 && input.zoom==0);
        input.update();assert(input.fit==0); // recenter is an event, not a held state
        auto panWrite=[&](int serial,int gesture,float x,float y,int active){std::ofstream file(path+".pan");file<<owner<<' '<<serial<<' '<<gesture<<' '<<x<<' '<<y<<' '<<active<<' '<<std::time(nullptr);};
        panWrite(1,1,10,-5,1);input.update();assert(input.panStarted && input.panActive && input.panX==10 && input.panY==-5 && input.zoom==0);
        input.update();assert(!input.panStarted && input.panActive && input.panX==0);
        panWrite(5,1,40,20,1);input.update();assert(input.panX==30 && input.panY==25);
        panWrite(6,1,40,20,0);input.update();assert(!input.panActive && input.panX==0);
        panWrite(7,2,-2,3,1);input.update();assert(input.panStarted && input.panX==-2 && input.panY==3);
        write(owner+" 13 0.4 13 6");input.update();assert(input.fit==0);
    }
    assert(!std::filesystem::exists(path+".active"));
    std::filesystem::remove_all(temp);
    std::cout<<"Smooth zoom, gaze-origin zoom, curved fit, center selection, mailbox coalescing and cleanup passed\n";
}
