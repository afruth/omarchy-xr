#include "tracking.hpp"
#include <cassert>
#include <iostream>
bool near(double a,double b) {return std::abs(a-b)<1e-5;}
using Vec=std::array<double,3>;
Vec transform(const std::array<float,16>& m,Vec v) {
    return {m[0]*v[0]+m[4]*v[1]+m[8]*v[2],m[1]*v[0]+m[5]*v[1]+m[9]*v[2],m[2]*v[0]+m[6]*v[1]+m[10]*v[2]};
}
void equal(Vec a,Vec b) {for(int i=0;i<3;++i) assert(near(a[i],b[i]));}
void packetParsing();
void prediction();
int main() {
    packetParsing();
    prediction();
    std::cout<<"SDK reference camera: six directions, 60 combined poses, yaw-only recenter, wrap and freshness passed\n";
}

void packetParsing() {
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
    predicted.prediction.horizonMs=30;
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
}

void prediction() {
    // The default cap is 20 ms, and a horizon of zero switches prediction off.
    tracking::Camera capped;
    assert(capped.accept("euler-nwu-v1 10 0 0 0",10) && capped.accept("euler-nwu-v1 10.01 0 0 10",10.01));
    assert(capped.predict(10.06,10.01) && near(capped.predictionMs,20));
    capped.prediction.horizonMs=0;
    assert(!capped.predict(10.06,10.01) && near(capped.view.y,tracking::conjugate(tracking::orientation(0,0,10)).y));
    // A still head with sensor jitter is shown as measured: 0.02 deg of alternating noise at 120 Hz is a
    // 4.8 deg/s two-point velocity, but under the 2 deg/s rest speed once fitted over five samples.
    tracking::Camera still;
    for(int i=0;i<8;++i){
        const double t=20+i/120.0;
        assert(still.accept("euler-nwu-v1 "+std::to_string(t)+" 0 0 "+std::to_string(i%2?0.02:-0.02),t+0.001));
    }
    assert(!still.predict(20+7/120.0+0.02,20+7/120.0) && still.predictionMs==0);
    // Steady motion predicts the whole horizon; between rest and full speed it fades in.
    auto sweep=[](double degreesPerSecond){
        tracking::Camera moving;
        for(int i=0;i<8;++i){
            const double t=30+i/120.0;
            assert(moving.accept("euler-nwu-v1 "+std::to_string(t)+" 0 0 "+std::to_string(degreesPerSecond*i/120.0),t+0.001));
        }
        moving.predict(30+7/120.0+0.02,30+7/120.0);
        return moving.predictionMs;
    };
    assert(sweep(1)==0 && std::abs(sweep(60)-20)<1e-3);
    assert(sweep(11)>5 && sweep(11)<15);
    // The fit crosses the yaw wrap and ignores samples older than a gap.
    tracking::Camera wrapped;
    for(int i=0;i<5;++i){
        const double t=40+i*0.01;
        assert(wrapped.accept("euler-nwu-v1 "+std::to_string(t)+" 0 0 "+std::to_string(std::remainder(178+i,360.0)),t+0.001));
    }
    assert(wrapped.predict(40.06,40.04));
    auto predictedView=tracking::conjugate(tracking::orientation(0,0,std::remainder(182+100*0.02-178,360.0)));
    assert(std::abs(wrapped.view.y-predictedView.y)<1e-4);
    tracking::Camera gapped;
    assert(gapped.accept("euler-nwu-v1 50 0 0 0",50) && gapped.accept("euler-nwu-v1 50.2 0 0 5",50.2));
    assert(!gapped.predict(50.22,50.2));
    // Settings line: versioned, bounded, no trailing junk.
    const auto parsed=tracking::parsePrediction("tracking-v1 12 3 30 6");
    assert(parsed && parsed->horizonMs==12 && parsed->restSpeed==3 && parsed->fullSpeed==30 && parsed->samples==6);
    for(const char* bad:{"","tracking-v2 12 3 30 6","tracking-v1 31 3 30 6","tracking-v1 -1 3 30 6","tracking-v1 12 30 30 6",
                         "tracking-v1 12 3 30 1","tracking-v1 12 3 30 9","tracking-v1 12 3 30 6 junk","tracking-v1 nan 3 30 6"})
        assert(!tracking::parsePrediction(bad));
}
