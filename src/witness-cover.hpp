#pragma once
#include "math.hpp"

namespace stalking {
struct SightMemory {
    Vec position{},velocity{};
    double age{};
    bool known{};

    void observe(std::optional<Vec> sight,double dt,double scale){
        age+=dt;
        if(!sight)return;
        if(known && age>0 && age<.35){
            Vec sample=(*sight-position)/age;sample.y=0;
            const double speed=length(sample);
            if(speed>7*scale)sample=sample*(7*scale/speed);
            velocity+=(sample-velocity)*(-std::expm1(-5*dt));
        }else velocity={};
        position=*sight;known=true;age=0;
    }
    Vec predicted() const{return position+velocity*std::min(.75,age);}
};

// Match the low rig: torso centered between hips and chest, head forward.
// Ray tests include both shoulders, rather than pretending a single point
// behind a tree conceals the entire creature.
struct Peek { Vec offset{}; double quality{}; };
template<class Blocked>
std::optional<Peek> peekAt(Vec feet,Vec eye,double scale,Blocked blocked){
    const Vec toward=unit(Vec{eye.x-feet.x,0,eye.z-feet.z});
    if(length(toward)<.5)return {};
    const Vec side{toward.z,0,-toward.x};
    const auto world=[&](Vec v){return feet+(side*v.x+Vec{0,v.y,0}+toward*v.z)*scale;};
    if(!blocked(eye,world({0,1.04,.05})))return {};
    const int concealed=int(blocked(eye,world({-.38,1.23,.44})))+
        int(blocked(eye,world({.38,1.23,.44})))+int(blocked(eye,world({0,.68,-.58})));
    if(concealed<2)return {};
    const Vec head=world({-.08,1.68,1.06});
    for(double amount:{0.,.34,.65,.92,1.12})for(double sign:{-1.,1.}){
        const Vec offset=side*(amount*sign);
        const Vec tip=head+offset*scale;
        if(blocked(eye,tip) || (amount>0 && blocked(head,tip)))continue;
        // Prefer just one exposed side of the skull over a full reveal.
        const bool edge=blocked(eye,tip-side*(sign*.24*scale));
        return Peek{offset,concealed+(edge?.8:0.)-.25*amount};
    }
    // A fully concealed position is still useful. Re-evaluate as the player
    // walks around the trunk; only expose the face when there is a clear edge.
    return Peek{{},concealed-1.8};
}
}
