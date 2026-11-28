#include "../src/witness-cover.hpp"
#include <cassert>
#include <iostream>
int main(){
    const Vec eye{0,1.45,-8};
    const Box trunk{{-1.1,0,-1.0},{1.1,9,1.0}};
    const auto blocked=[&](Vec from,Vec to){
        const Vec delta=to-from;
        return intersectRay(from,unit(delta),trunk,length(delta)-.015).has_value();
    };
    assert(!stalking::peekAt({4,0,3},eye,1,blocked));
    const auto hidden=stalking::peekAt({0,0,3},eye,1,blocked);
    assert(hidden);
    int peeks=0;
    for(double x=-1.5;x<=1.5;x+=.05){
        const Vec feet{x,0,3};
        if(auto p=stalking::peekAt(feet,eye,1,blocked);p && length(p->offset)>.1){
            const Vec toward=unit(Vec{eye.x-feet.x,0,eye.z-feet.z});
            const Vec side{toward.z,0,-toward.x};
            const Vec head=feet+side*(-.08)+toward*1.06+Vec{0,1.68,0};
            assert(blocked(eye,feet+toward*.05+Vec{0,1.04,0}));
            assert(!blocked(eye,head+p->offset));++peeks;
        }
    }
    assert(peeks>0);
    stalking::SightMemory track;
    for(int i=0;i<=60;i++)track.observe(Vec{i/30.,1.45,0},1./60,1);
    assert(track.predicted()==track.position && track.velocity.x>1.9);
    track.observe({},.5,1);
    assert(track.predicted().x>2.9 && track.predicted().x<3.01);
    track.observe({},1,1);
    const Vec lastLead=track.predicted();
    track.observe({},3,1);
    assert(track.predicted()==lastLead);
    track.observe(Vec{-4,2,3},1./60,1);
    assert(track.velocity==Vec{} && track.age==0 && track.predicted()==(Vec{-4,2,3}));
    std::cout<<"PASS: "<<peeks<<" valid side-peek positions; open ground rejected, full concealment accepted\n";
}
