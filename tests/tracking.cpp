#include "tracking.hpp"
#include <cassert>
#include <iostream>
bool near(double a,double b) {return std::abs(a-b)<1e-5;}
using Vec=std::array<double,3>;
Vec transform(const std::array<float,16>& m,Vec v) {
    return {m[0]*v[0]+m[4]*v[1]+m[8]*v[2],m[1]*v[0]+m[5]*v[1]+m[9]*v[2],m[2]*v[0]+m[6]*v[1]+m[10]*v[2]};
}
void equal(Vec a,Vec b) {for(int i=0;i<3;++i) assert(near(a[i],b[i]));}
int main() {
    tracking::Camera c;
    assert(!c.fresh(10));
    assert(c.accept("euler-nwu-v1 10 0 0 123",10));
    assert(near(c.view.w,1));
    assert(!c.accept("euler-nwu-v1 10 0 0 123",10));
    assert(!c.accept("11 1 0 0 0",11)); // reject obsolete quaternion packets
    assert(!c.accept("euler-nwu-v1 11 nan 0 0",11));
    assert(!c.accept("euler-nwu-v1 11 0 0 0 junk",11));
    assert(!c.accept("euler-nwu-v1 12 0 0 0",11));
    assert(!c.accept("euler-nwu-v1 10.1 0 0 0",11));
    tracking::Camera timed;
    assert(timed.accept("euler-nwu-v2 1 4 5 6 4242",1) && timed.deviceTimestamp==4242);
    assert(!timed.accept("euler-nwu-v2 1.1 0 0 0",1.1));
    // SDK Gen1/Gen2 reference directions. Viewed world moves opposite the head.
    auto view=[](double r,double p,double y){return tracking::matrix(tracking::conjugate(tracking::orientation(r,p,y)));};
    equal(transform(view(0,0,90),{-1,0,0}),{0,0,-1}); // looking left
    equal(transform(view(0,0,-90),{1,0,0}),{0,0,-1}); // looking right
    equal(transform(view(0,90,0),{0,-1,0}),{0,0,-1}); // looking down
    equal(transform(view(0,-90,0),{0,1,0}),{0,0,-1}); // looking up
    equal(transform(view(90,0,0),{1,0,0}),{0,1,0}); // right ear down
    equal(transform(view(-90,0,0),{-1,0,0}),{0,1,0});
    // SDK demo's independent forward/up-vector construction is the oracle.
    constexpr double rad=3.14159265358979323846/180;
    for(double y:{-150.,-30.,0.,60.,179.}) for(double p:{-70.,0.,35.,80.}) for(double r:{-45.,0.,30.}) {
        Vec forward={-std::sin(y*rad)*std::cos(p*rad),-std::sin(p*rad),-std::cos(y*rad)*std::cos(p*rad)};
        Vec right={std::cos(y*rad),0,-std::sin(y*rad)};
        Vec up={right[1]*forward[2]-right[2]*forward[1],right[2]*forward[0]-right[0]*forward[2],right[0]*forward[1]-right[1]*forward[0]};
        for(int i=0;i<3;++i) up[i]=up[i]*std::cos(r*rad)+right[i]*std::sin(r*rad);
        equal(transform(view(r,p,y),forward),{0,0,-1});
        equal(transform(view(r,p,y),up),{0,1,0});
    }
    // A tilted startup/recenter must preserve gravity, never redefine all axes.
    assert(c.accept("euler-nwu-v1 11 25 -20 160",11));
    c.recenter(11.1);
    auto expected=view(25,-20,0),actual=tracking::matrix(c.view);
    for(int i=0;i<16;++i) assert(near(expected[i],actual[i]));
    assert(c.accept("euler-nwu-v1 11.2 0 0 -170",11.2)); // crosses yaw wrap: +30
    actual=tracking::matrix(c.view);expected=view(0,0,30);
    for(int i=0;i<16;++i) assert(near(expected[i],actual[i]));
    assert(!c.fresh(12));c.recenter(12);assert(!c.centered);
    auto held=tracking::matrix(c.view);assert(held==actual);
    assert(c.accept("euler-nwu-v1 13 0 0 -150",13));assert(near(c.view.w,1));
    tracking::Camera predicted;
    assert(predicted.accept("euler-nwu-v1 10 0 0 0",10));
    assert(!predicted.predict(10.01,10));
    assert(predicted.accept("euler-nwu-v1 10.01 0 0 10",10.01));
    assert(predicted.predict(10.02,10.01));
    auto predictedView=tracking::conjugate(tracking::orientation(0,0,20));
    assert(near(predicted.view.w,predictedView.w) && near(predicted.view.y,predictedView.y));
    assert(near(predicted.predictionMs,10));
    assert(predicted.predict(10.06,10.01));
    predictedView=tracking::conjugate(tracking::orientation(0,0,40));
    assert(near(predicted.view.w,predictedView.w) && near(predicted.view.y,predictedView.y));
    assert(near(predicted.predictionMs,30));
    assert(!predicted.predict(10.06,11));
    std::cout<<"SDK reference camera: six directions, 60 combined poses, yaw-only recenter, wrap and freshness passed\n";
}
