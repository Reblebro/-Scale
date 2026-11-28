// Lightweight local rig test: no SDL, graphics, or game assets required.
#include "../src/contact-rig.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

static void require(bool condition,const char* message){
    if(!condition)throw std::runtime_error(message);
}
static void finite(Vec v){
    require(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z),"non-finite rig point");
}
int main(){
    contact::Queries scene;
    scene.cast=[](Vec from,Vec direction,double length)->std::optional<contact::Surface>{
        if(direction.y>=-1e-9 || from.y<=0)return {};
        const double t=-from.y/direction.y;
        if(t<0 || t>length)return {};
        return contact::Surface{from+direction*t,{0,1,0},{}};
    };
    scene.follow=[](contact::Surface&){return true;};
    contact::Rig rig;
    const contact::Frame frame{{0,0,0},1.0,0};
    for(int i=0;i<120;++i)rig.update(frame,true,false,false,{},5.1,1.0/60,scene);
    require(rig.crouch>.99,"should remain low in crawl");
    for(int i=0;i<2;++i){
        require(rig.pose.arms[i].planted,"hands plant on the ground");
        require(rig.pose.legs[i].planted,"hind feet plant on the ground");
        require(rig.pose.arms[i].tip.z>rig.pose.legs[i].tip.z+2.0,"front hands far ahead of hind feet");
    }
    const double crawlHead=rig.pose.neck.y;
    require(crawlHead<2.1,"crawling head exceeds low clearance");
    for(auto arm:rig.pose.arms){finite(arm.root);finite(arm.joint);finite(arm.tip);}
    for(auto leg:rig.pose.legs){finite(leg.root);finite(leg.joint);finite(leg.tip);}
    // Deliberate rise is continuous and slower than dropping back down.
    rig.update(frame,false,false,false,{},5.1,1.0/60,scene);
    require(rig.crouch>.85,"stand animation rose too abruptly");
    for(int i=0;i<120;++i)rig.update(frame,false,false,false,{},5.1,1.0/60,scene);
    require(rig.crouch<.02,"upright pose failed to finish");
    require(rig.pose.neck.y>crawlHead+1.0,"upright pose did not raise head");
    // Low ceiling must immediately override an attempted rise.
    rig.update(frame,false,false,false,{},2.15,1.0/60,scene);
    require(rig.crouch>.99,"low ceiling must force the crawl");
    for(int i=0;i<120;++i)rig.update(frame,true,false,false,{},5.1,1.0/60,scene);
    require(rig.pose.neck.y<2.1,"crawl pose did not recover");
    std::cout<<"PASS: crawl stance, gradual rise, overhead obstruction, limbs finite\n";
}
