#include "tracking.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
bool near(double a,double b) {return std::abs(a-b)<1e-5;}
using Vec=std::array<double,3>;
Vec transform(const std::array<float,16>& m,Vec v) {
    return {m[0]*v[0]+m[4]*v[1]+m[8]*v[2],m[1]*v[0]+m[5]*v[1]+m[9]*v[2],m[2]*v[0]+m[6]*v[1]+m[10]*v[2]};
}
void equal(Vec a,Vec b) {for(int i=0;i<3;++i) assert(near(a[i],b[i]));}
// Exactness tests run with the stabiliser off; it is tested on its own in prediction().
tracking::Camera unfiltered() { tracking::Camera c; c.prediction.minCutoff=0; return c; }
void packetParsing();
void prediction();
void grab();
void stabilization();
int main() {
    packetParsing();
    prediction();
    grab();
    stabilization();
    std::cout<<"SDK reference camera: six directions, 60 combined poses, yaw-only recenter, wrap and freshness passed\n";
}

void packetParsing() {
    tracking::Camera c=unfiltered();
    assert(!c.fresh(10));
    assert(c.accept("euler-nwu-v1 10 0 0 123",10));
    assert(near(c.view.w,1));
    assert(!c.accept("euler-nwu-v1 10 0 0 123",10));
    assert(!c.accept("11 1 0 0 0",11)); // reject obsolete quaternion packets
    assert(!c.accept("euler-nwu-v1 11 nan 0 0",11));
    assert(!c.accept("euler-nwu-v1 11 0 0 0 junk",11));
    assert(!c.accept("euler-nwu-v1 12 0 0 0",11));
    assert(!c.accept("euler-nwu-v1 10.1 0 0 0",11));
    tracking::Camera timed=unfiltered();
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
    tracking::Camera predicted=unfiltered();
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
    tracking::Camera capped=unfiltered();
    assert(capped.accept("euler-nwu-v1 10 0 0 0",10) && capped.accept("euler-nwu-v1 10.01 0 0 10",10.01));
    assert(capped.predict(10.06,10.01) && near(capped.predictionMs,20));
    capped.prediction.horizonMs=0;
    assert(!capped.predict(10.06,10.01) && near(capped.view.y,tracking::conjugate(tracking::orientation(0,0,10)).y));
    // A still head with sensor jitter is shown as measured: 0.02 deg of alternating noise at 120 Hz is a
    // 4.8 deg/s two-point velocity, but under the 2 deg/s rest speed once fitted over five samples.
    tracking::Camera still=unfiltered();
    for(int i=0;i<8;++i){
        const double t=20+i/120.0;
        assert(still.accept("euler-nwu-v1 "+std::to_string(t)+" 0 0 "+std::to_string(i%2?0.02:-0.02),t+0.001));
    }
    assert(!still.predict(20+7/120.0+0.02,20+7/120.0) && still.predictionMs==0);
    // Steady motion predicts the whole horizon; between rest and full speed it fades in.
    auto sweep=[](double degreesPerSecond){
        tracking::Camera moving=unfiltered();
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
    tracking::Camera wrapped=unfiltered();
    for(int i=0;i<5;++i){
        const double t=40+i*0.01;
        assert(wrapped.accept("euler-nwu-v1 "+std::to_string(t)+" 0 0 "+std::to_string(std::remainder(178+i,360.0)),t+0.001));
    }
    assert(wrapped.predict(40.06,40.04));
    auto predictedView=tracking::conjugate(tracking::orientation(0,0,std::remainder(182+100*0.02-178,360.0)));
    assert(std::abs(wrapped.view.y-predictedView.y)<1e-4);
    tracking::Camera gapped=unfiltered();
    assert(gapped.accept("euler-nwu-v1 50 0 0 0",50) && gapped.accept("euler-nwu-v1 50.2 0 0 5",50.2));
    assert(!gapped.predict(50.22,50.2));
    // Settings line: versioned, bounded, no trailing junk. v1 keeps the default filter.
    const auto parsed=tracking::parsePrediction("tracking-v1 12 3 30 6");
    assert(parsed && parsed->horizonMs==12 && parsed->restSpeed==3 && parsed->fullSpeed==30 && parsed->samples==6 && parsed->minCutoff==0.7 && parsed->beta==0.25);
    const auto parsed2=tracking::parsePrediction("tracking-v2 12 3 30 6 0.5 0.1");
    assert(parsed2 && parsed2->minCutoff==0.5 && parsed2->beta==0.1);
    for(const char* bad:{"","tracking-v3 12 3 30 6","tracking-v2 12 3 30 6","tracking-v2 12 3 30 6 31 0.1","tracking-v2 12 3 30 6 1 -1",
                         "tracking-v1 31 3 30 6","tracking-v1 -1 3 30 6","tracking-v1 12 30 30 6",
                         "tracking-v1 12 3 30 1","tracking-v1 12 3 30 9","tracking-v1 12 3 30 6 junk","tracking-v1 nan 3 30 6"})
        assert(!tracking::parsePrediction(bad));

    // Stabiliser. A still head with sensor noise: 0.1 degree peak-to-peak alternating yaw at 120 Hz
    // comes out at least ten times smaller, and the motion is recognised as incoherent.
    auto feed=[](tracking::Camera& cam,double t0,int count,auto yawOf){ for(int i=0;i<count;++i){ const double t=t0+i/120.0; assert(cam.accept("euler-nwu-v1 "+std::to_string(t)+" 0 0 "+std::to_string(yawOf(i,t)),t+0.001)); } };
    tracking::Camera noisy;
    feed(noisy,100,240,[](int i,double){ return i%2 ? 0.05 : -0.05; });
    assert(std::abs(noisy.yaw)<0.005 && noisy.coherence<0.3 && noisy.cutoffHz<1.5);
    // A deliberate 30 deg/s turn is coherent, opens the filter and lags it by well under a degree.
    tracking::Camera turning;
    feed(turning,200,240,[](int,double t){ return 30*(t-200); });
    assert(turning.coherence>0.95 && turning.cutoffHz>5);
    // A shake riding on a slow drift still scores as incoherent enough to keep the filter closed:
    // 3 deg/s drift plus a 6 Hz, half-degree wobble.
    // The drift itself lags by a constant amount, so the wobble that gets through is the spread of
    // (filtered - drift) over the last two seconds: under a fifth of the raw one-degree swing.
    tracking::Camera drifting;
    double lo=1e9, hi=-1e9;
    for(int i=0;i<360;++i){
        const double t=250+i/120.0, base=3*(t-250);
        assert(drifting.accept("euler-nwu-v1 "+std::to_string(t)+" 0 0 "+std::to_string(base+0.5*std::sin(2*3.14159265358979323846*6*(t-250))),t+0.001));
        if(i>=120){ lo=std::min(lo,drifting.yaw-base); hi=std::max(hi,drifting.yaw-base); }
    }
    assert(drifting.coherence<0.6 && drifting.cutoffHz<1.2 && hi-lo<0.2);
    assert(turning.headSpeed()>25 && turning.headSpeed()<35);   // the dwell's speed follows the stabilised turn
    const double turnLag=30*(240/120.0-1/120.0)-turning.yaw;
    assert(turnLag>0 && turnLag<0.7);
    // Prediction from the filtered turn covers most of that lag.
    assert(turning.predict(200+239/120.0+0.02,200+239/120.0+0.001));
    // An 8 Hz shake of one degree amplitude is fast but incoherent: it stays smoothed to a fraction
    // of its amplitude, and prediction does not chase it.
    tracking::Camera shaking;
    double peak=0;
    for(int i=0;i<360;++i){
        const double t=300+i/120.0;
        assert(shaking.accept("euler-nwu-v1 "+std::to_string(t)+" 0 0 "+std::to_string(std::sin(2*3.14159265358979323846*8*(t-300))),t+0.001));
        if(i>=240) peak=std::max(peak,std::abs(shaking.yaw));
    }
    assert(peak<0.3 && shaking.coherence<0.3);
    assert(shaking.headSpeed()<15);   // a smoothed shake still counts as a settled head for the dwell
    shaking.predict(300+359/120.0+0.02,300+359/120.0+0.001);
    assert(shaking.predictionMs<6);
    // After a gap the filter restarts on the new sample instead of slewing towards it.
    tracking::Camera gap;
    feed(gap,400,60,[](int,double){ return 0.0; });
    assert(gap.accept("euler-nwu-v1 401 0 0 45",401.001) && std::abs(gap.yaw-45)<1e-9);
    // The device clock unit is learned: microsecond counters become seconds for the velocity fit.
    tracking::Camera clocked=unfiltered();
    for(int i=0;i<30;++i){ const double t=500+i/120.0; assert(clocked.accept("euler-nwu-v2 "+std::to_string(t)+" 0 0 0 "+std::to_string(int64_t(t*1e6)),t+0.001)); }
    assert(clocked.deviceScale==1e-6);
    // Filter off is a pass-through.
    tracking::Camera off=unfiltered();
    feed(off,600,10,[](int i,double){ return i%2 ? 0.05 : -0.05; });
    assert(std::abs(std::abs(off.yaw)-0.05)<1e-9 && off.cutoffHz==0);
}

// Grab (docs/xr-controls-plan.md §5.7): a yaw sweep while grabbing keeps the view's heading, release keeps
// the reached reference, and a grab never starts before the first sample.
void grab() {
    auto view=[](double r,double p,double y){return tracking::matrix(tracking::conjugate(tracking::orientation(r,p,y)));};
    auto same=[](std::array<float,16> a,std::array<float,16> b){for(int i=0;i<16;++i) assert(near(a[i],b[i]));};
    tracking::Camera c=unfiltered();
    assert(!c.grab(true) && !c.grabbing);
    assert(c.accept("euler-nwu-v1 20 0 0 10",20));          // neutral 10
    assert(c.accept("euler-nwu-v1 20.01 0 5 40",20.01));   // looking 30° left at something
    same(tracking::matrix(c.view),view(0,5,30));
    assert(c.grab(true) && c.grabbing);
    for(double y:{60.,120.,-170.,-100.,10.}) {              // sweep, across the wrap: the view stays put
        static double t=20.02; t+=.01;
        assert(c.accept("euler-nwu-v1 "+std::to_string(t)+" 0 5 "+std::to_string(y),t));
        same(tracking::matrix(c.view),view(0,5,30));
    }
    c.grab(false);
    assert(near(c.neutralYaw,-20));                          // 10 at the end, the view kept at 30
    assert(c.accept("euler-nwu-v1 20.2 0 0 -20",20.2));    // straight ahead now shows what was at 30° left
    same(tracking::matrix(c.view),view(0,0,0));
    std::cout<<"Grab: the view holds through a yaw sweep across the wrap, release keeps the new heading passed\n";
}

// Stabilization above 1x: a hold zone that keeps small movements from moving anything while a
// deliberate turn is still followed exactly. Yaw is fed at 240 Hz with 0.01 degree of sensor noise.
struct Run { double moved=0, last=0, headSpeed=0; };
Run steer(tracking::Camera& c, double t0, double seconds, double (*yawOf)(double), double measureFrom=0) {
    Run r; double prev=0; bool first=true;
    for(int i=0;i<int(seconds*240);++i){
        const double t=t0+i/240.0, noise=0.01*std::sin(i*12.9898);
        assert(c.accept("euler-nwu-v1 "+std::to_string(t)+" 0 0 "+std::to_string(yawOf(t-t0)+noise),t+0.0005));
        if(t-t0>=measureFrom){ if(!first) r.moved+=std::abs(c.yaw-prev); first=false; r.headSpeed=std::max(r.headSpeed,c.headSpeed()); }
        prev=c.yaw;
    }
    r.last=c.yaw; return r;
}
tracking::Camera steady(double strength) { tracking::Camera c; c.strength=strength; return c; }
void stabilization() {
    constexpr double pi=3.14159265358979323846;
    assert(tracking::parseStabilization("stabilization-v1 2.5")==2.5 && tracking::parseStabilization("stabilization-v1 1")==1);
    for(const char* bad:{"","stabilization-v1","stabilization-v1 0.5","stabilization-v1 4.5","stabilization-v1 nan",
                         "stabilization-v1 2 junk","stabilization-v2 2","tracking-v2 12 3 30 6 0.5 0.1"})
        assert(!tracking::parseStabilization(bad));
    // Breathing sway (0.3 degree at 0.25 Hz) plus a 9 Hz tremor: 1x lets it through, 3x and 4x hold the
    // image still and the dwell sees a settled head.
    auto sway=[](double t){ return 0.3*std::sin(2*pi*0.25*t)+0.15*std::sin(2*pi*9*t); };
    auto base=steady(1), firm=steady(3), firmest=steady(4);
    assert(steer(base,0,4,sway,1).moved>0.5);
    assert(steer(firm,0,4,sway,1).moved<0.01);
    const Run held=steer(firmest,0,4,sway,1);
    assert(held.moved<0.01 && held.headSpeed<0.5);
    // A slow half-degree drift at 1 deg/s: held at 4x, half of it shows at 2x.
    auto drift=[](double t){ return t<1 ? 0.0 : std::min(0.5,t-1); };
    auto gentle=steady(2), still=steady(4);
    const double shown2=steer(gentle,10,3,drift).last, shown4=steer(still,10,3,drift).last;
    assert(shown2>0.15 && shown2<0.35 && std::abs(shown4)<0.02);
    // A deliberate 30 degree turn is followed and ends exactly where the head stops ...
    auto turn=[](double t){ const double x=std::clamp((t-1)/0.5,0.0,1.0); return 30*x*x*(3-2*x); };
    auto turning=steady(4);
    assert(std::abs(steer(turning,20,3,turn).last-30)<0.02);
    // ... and the zone re-centres there: a further 0.3 degree nudge in the turn's direction moves nothing.
    assert(steer(turning,23,2,[](double t){ return 30+0.3*std::clamp(t/0.3,0.0,1.0); }).moved<0.01);
    // A slow 3 deg/s, 10 degree turn is followed too; the filter's settling tail stays inside the zone,
    // so the image stops when the head does.
    auto slow=steady(4);
    const double reached=steer(slow,30,6,[](double t){ return t<1 ? 0.0 : std::min(10.0,3*(t-1)); }).last;
    assert(reached>9.25 && reached<=10.01);
    assert(steer(slow,36,2,[](double){ return 10.0; }).moved<0.01);
    // Lowering the level live drops the zone at once: the image returns to the filtered head pose.
    auto live=steady(4);
    steer(live,40,2,[](double t){ return t<1 ? 0.0 : 0.5; });
    assert(std::abs(live.yaw)<0.02);
    live.strength=1;
    steer(live,42,0.1,[](double){ return 0.5; });
    assert(std::abs(live.yaw-0.5)<0.02);
    std::cout<<"Stabilization: sway, tremor and slow drift held; turns followed exactly; zone re-centres; live level change passed\n";
}
