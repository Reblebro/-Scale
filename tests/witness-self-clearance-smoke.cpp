#include "../src/contact-rig.hpp"
#include <iostream>
#include <stdexcept>
static void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
int main(){
    using namespace contact;
    // Direct crossing and parallel segment regressions.
    Chain a{{-1,1,0},{0,1,0},{1,1,0}},b{{0,1,-1},{0,1,0},{0,1,1}};
    check(limbClearance(a,true,b,false)<0,"crossing was not detected");
    b.root.y=b.joint.y=b.tip.y=2;
    check(limbClearance(a,true,b,false)>.4,"separate limbs collide");
    Queries scene;
    scene.cast=[](Vec from,Vec dir,double reach)->std::optional<Surface>{
        if(dir.y>=-1e-9 || from.y<=0)return {};
        const double t=-from.y/dir.y;if(t<0 || t>reach)return {};
        return Surface{from+dir*t,{0,1,0},{}};
    };
    scene.follow=[](Surface&){return true;};
    Rig rig;
    double minimum=1e9, worst=1e9;int overlaps=0,frames=0,steady=0;
    Vec pos{};
    for(int i=0;i<1500;++i){
        const bool low=i<600 || i>=900;
        const double heading=.46*std::sin(i*.011);
        if(i>=240 && i<1080)pos+=Vec{std::sin(heading),0,std::cos(heading)}*.018;
        rig.update({pos,1,heading},low,false,false,{},5.1,1.0/60,scene);
        std::array<Chain,4> chains={rig.pose.legs[0],rig.pose.legs[1],rig.pose.arms[0],rig.pose.arms[1]};
        bool collision=false;
        for(int j=0;j<4;j++){
            const auto& c=chains[j];const auto bones=j>=2?rig.anatomy.arm:rig.anatomy.leg;
            check(std::abs(length(c.root-c.joint)-bones.upper)<1e-7,"upper length changed");
            check(std::abs(length(c.joint-c.tip)-bones.lower)<1e-7,"lower length changed");
            for(int k=0;k<j;k++){
                const double d=limbClearance(c,j>=2,chains[k],k>=2);
                worst=std::min(worst,d);if(d<-.003)collision=true;
                if(i>=120 && i<240)minimum=std::min(minimum,d);
            }
        }
        overlaps+=collision;++frames;
        if(i>=120 && i<240 && chains[0].planted && chains[1].planted && chains[2].planted && chains[3].planted)++steady;
    }
    std::cout<<"Self-clearance: "<<frames<<" frames, "<<overlaps<<" overlapping frames; minimum idle margin "<<minimum<<", worst "<<worst<<", planted idle frames "<<steady<<"\n";
    check(minimum>=0 && steady>100,"idle contact/clearance regression");
    check(overlaps==0,"limb overlap in animated sequence");
}
