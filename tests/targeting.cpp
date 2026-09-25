#include "targeting.hpp"
#include "camera_controls.hpp"
#include "canvas_model.hpp"
#include <numeric>
#include <random>
#include <cassert>
#include <iostream>
using namespace targeting;
bool close(float a,float b){return std::abs(a-b)<1e-4f;}
bool sameHit(const std::optional<Hit>& a,const std::optional<Hit>& b){
    if(!a || !b)return !a && !b;
    return a->output==b->output && a->distance==b->distance && a->u==b->u && a->v==b->v;
}
// The canvas culls by heading before ray tests: on a 60-window ring the culled query must
// find exactly what the brute-force query finds.
void culledCanvas(){
    const canvas::Ring ring{};const auto cyl=canvas::cylinder(ring,60);
    std::vector<PanelLayout> ring60;
    for(int i=0;i<60;++i){
        const int row=i%3-1;const float x=float(i/3)*ring.period()/20;
        ring60.push_back({"0x"+std::to_string(100+i),x,row*850.f-350,400+float(i%4)*60,700});
    }
    std::vector<size_t> all(ring60.size());std::iota(all.begin(),all.end(),0);
    std::mt19937 rng(11);std::uniform_real_distribution<float> heading(-180,180),pitch(-25,25),pan(-.2f,.2f);
    int hits=0;
    for(int n=0;n<200;++n){
        const float h=heading(rng),p=pitch(rng);
        const Vec direction={std::sin(h*spatial::pi/180)*std::cos(p*spatial::pi/180),std::sin(p*spatial::pi/180),-std::cos(h*spatial::pi/180)*std::cos(p*spatial::pi/180)};
        const Ray ray{{pan(rng),pan(rng),pan(rng)},direction};
        const auto candidates=canvas::visibleIndices(ring60,h,24+10,ring);
        const auto brute=query(ray,ring60,cyl.cx,cyl.cy,cyl.span,cyl.distance,cyl.workspace);
        assert(sameHit(query(ray,ring60,cyl.cx,cyl.cy,cyl.span,cyl.distance,cyl.workspace,&candidates),brute));
        assert(sameHit(query(ray,ring60,cyl.cx,cyl.cy,cyl.span,cyl.distance,cyl.workspace,&all),brute));
        assert(candidates.size()<ring60.size());
        hits+=brute.has_value();
    }
    assert(hits>20);
}
int main(){
    PanelLayout panel{"one",0,0,1800,900};
    spatial::Pose flat{{0,0,-4},0,0};
    auto h=intersect({{0,0,0},{0,0,-1}},panel,flat);
    assert(h && h->output=="one" && close(h->distance,4) && close(h->u,.5) && close(h->v,.5));
    assert(close(h->pixelX,900) && close(h->pixelY,450));
    h=intersect({{0,0,0},normalize({.5f,.25f,-4})},panel,flat);
    assert(h && close(h->u,.75) && close(h->v,.25));
    assert(!intersect({{0,0,0},{0,0,1}},panel,flat));
    assert(!intersect({{0,0,0},{1,0,0}},panel,flat));
    assert(!intersect({{0,0,0},normalize({3,0,-4})},panel,flat));
    // Curved and rotated panels: target points on the exact rendered mesh.
    for(float bend:{0.f,.2f,.8f})for(float yaw:{-.8f,0.f,.8f}){
        spatial::Pose pose{{1,.3f,-4},yaw,bend};
        int n=spatial::surfaceSegments(2,bend);
        for(int i=0;i<n;++i){
            const float u=(i+.37f)/n;
            const auto a=spatial::vertex(pose,(float(i)/n-.5f)*2,.1f);
            const auto b=spatial::vertex(pose,(float(i+1)/n-.5f)*2,.1f);
            const auto point=add(mul(a,.63f),mul(b,.37f));
            const auto normal=normalize(cross(sub(b,a),{0,1,0}));
            auto hit=intersect({add(point,mul(normal,.5f)),mul(normal,-1)},panel,pose);
            assert(hit && close(hit->u,u) && close(hit->v,.4f));
        }
    }
    // Nearest actual surface wins, independent of layout order.
    std::vector<PanelLayout> overlapping{{"far",0,0,1800,900,0},{"curved",0,0,1800,900,100}};
    auto q=query({{0,0,0},normalize({.5f,0,-4})},overlapping,900,450,2,4,0);
    assert(q && q->output=="curved");
    std::reverse(overlapping.begin(),overlapping.end());
    assert(query({{0,0,0},normalize({.5f,0,-4})},overlapping,900,450,2,4,0)->output=="curved");
    // Real gutter remains empty: no nearest-center fallback.
    std::vector<PanelLayout> separated{{"left",-1824,0,1800,900},{"right",24,0,1800,900}};
    assert(!query({{0,0,0},{0,0,-1}},separated,0,450,4,4,0));
    // The inverse ray must match the full rendering transform, including mouse
    // rotations, head roll/pitch/yaw, and focus translation along all three axes.
    for(float yaw:{-80.f,0.f,70.f})for(float pitch:{-30.f,0.f,40.f}){
        auto view=viewRotation(tracking::conjugate(tracking::orientation(25,pitch,yaw)),12,-18);
        Vec pan{1,-2,.7f};auto ray=viewRay(view,pan);
        auto inEye=rotate(view,add(add(ray.origin,mul(ray.direction,4)),pan));
        assert(close(inEye.x,0) && close(inEye.y,0) && close(inEye.z,-4));
    }
    assert(!intersect({{NAN,0,0},{0,0,-1}},panel,flat));
    assert(!intersect({{0,0,0},{0,0,0}},panel,flat));
    // Focus centers the target along the current head ray and fits all vertical
    // mesh extrema without modifying the view rotation or its calibration.
    for(float bend:{0.f,.7f})for(float roll:{0.f,35.f}) {
        spatial::Pose pose{{2,1,-4},.6f,bend};
        auto view=viewRotation(tracking::conjugate(tracking::orientation(roll,20,-30)),0,0);
        const auto pan=navigation::fitPanForHeight(panel,pose,view,28);
        const auto center=rotate(view,add(pose.center,pan));
        assert(close(center.x,0) && close(center.y,0) && center.z<0);
        for(int i=0;i<=100;++i)for(float y:{-.5f,.5f}){
            const auto v=rotate(view,add(spatial::vertex(pose,(float(i)/100-.5f)*2,y),pan));
            assert(v.z<0 && std::abs(v.y)/-v.z < std::tan(28*spatial::pi/360));
        }
    }
    Tracker tracker;
    tracker.update(h,true);assert(tracker.transition==Transition::Enter);
    tracker.update(h,true);assert(tracker.transition==Transition::Move);
    auto second=*h;second.output="two";
    tracker.update(second,true);assert(tracker.transition==Transition::Switch);
    tracker.update(second,false);assert(!tracker.current && tracker.transition==Transition::Leave);
    tracker.update({},true);assert(tracker.transition==Transition::None);
    Selection selection;
    selection.observe(h);assert(selection.output==h->output);
    selection.observe({});assert(selection.output==h->output); // looking at keyboard
    selection.observe(second);assert(selection.output=="two");
    selection.validate({{"two",0,0,1920,1080},{"other",2000,0,1920,1080}});
    assert(selection.output=="two"); // stable identity, not array position
    selection.validate({{"other",0,0,1920,1080}});assert(selection.output.empty());
    culledCanvas();
    std::cout<<"Targeting: canvas candidate culling, flat/curved surfaces, UV/pixels, nearest hit, gaps, view transforms and freshness passed\n";
}
