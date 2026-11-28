#include "../src/contact-rig.hpp"
#include "../src/atmosphere.hpp"
#include "../src/goblin-rig.hpp"
#include "../src/witness-horror.hpp"
#include <cassert>
#include <iostream>

int main(){
    using namespace contact;
    // Test quiet standing and crawling at high refresh rates. No contact
    // changes or inputs occur: a solver flip is not an animation.
    for(bool crawl:{false,true}){
        long casts=0,start=0;Queries scene;
        scene.cast=[&](Vec p,Vec d,double r)->std::optional<Surface>{
            ++casts;if(d.y>=-1e-9||p.y<=0)return {};
            const double t=-p.y/d.y;if(t<0||t>r)return {};
            return Surface{p+d*t,{0,1,0},{}};
        };
        Rig rig;Pose last{};double travel=0,peak=0;int releases=0;
        for(int i=0;i<1200;i++){
            rig.update({{},1,0},crawl,false,false,{},5.1,1./120,scene);
            if(i==239)start=casts;
            if(i>=240)for(int j=0;j<2;j++)for(auto pair:{std::pair{rig.pose.arms[j],last.arms[j]},std::pair{rig.pose.legs[j],last.legs[j]}}){
                const double d=length(pair.first.tip-pair.second.tip)+length(pair.first.joint-pair.second.joint);
                travel+=d;peak=std::max(peak,d);releases+=pair.second.planted&&!pair.first.planted;
            }
            last=rig.pose;
        }
        assert(travel<1.0 && peak<.004 && releases==0);
        assert(casts-start<43000);
        std::cout<<(crawl?"Crawl":"Standing")<<" idle: "<<travel<<" total joint/endpoint travel, "<<peak<<" peak, "<<casts-start<<" ray queries.\n";
        // Confirm the moving digits retain the mesh topology and unit normals.
        if(crawl){
            WitnessSkin skin;skin.build(false);Geometry open,closed;
            skin.pose(open,rig.pose);auto held=rig.pose;held.arms[1].grip=1;
            skin.pose(closed,held);assert(open.points.size()==closed.points.size());
            double max=0;std::size_t changed=0;
            for(std::size_t i=0;i<closed.points.size();i++){
                auto a=open.points[i],b=closed.points[i];
                const double d=length({a.x-b.x,a.y-b.y,a.z-b.z});max=std::max(max,d);changed+=d>1e-5;
                assert(std::abs(length({b.nx,b.ny,b.nz})-1)<.003);
            }
            assert(max>.06 && changed>100 && changed<closed.points.size()/5);
            std::cout<<"Feeding fingers: "<<changed<<" moved vertices, same topology and normalized normals.\n";
        }
    }
    const auto transformed=[](const Vertex& v,double angle,double support){
        return Vec{v.x,v.y*std::cos(angle)+v.z*std::sin(angle)+support,-v.y*std::sin(angle)+v.z*std::cos(angle)};
    };
    // Each of the three roles begins from an active movement/attack pose.
    // Evaluate each collapse at 120 Hz with actual geometry, including the
    // crossbow's support-hand release. The final pose must equal its cache.
    constexpr int hz=120;
    const int frames=int(std::ceil(dread::FallDuration*hz));
    for(int role=0;role<3;role++){
        settlement::GoblinRig rig;rig.build(true,role);
        Geometry last,next,settled;double peak=0,priorAngle=0,priorSupport=0;
        for(int frame=0;frame<=frames;frame++){
            const double t=std::min(1.,frame/(hz*dread::FallDuration));
            rig.pose(next,.72,.68,1.3,t,.43,.44,-1,.65);
            const double angle=dread::fallAngle(t),support=dread::supportHeight(next.points,angle);
            assert(!next.points.empty());
            if(frame){assert(next.points.size()==last.points.size());
                for(std::size_t i=0;i<next.points.size();i+=7){
                    const auto a=transformed(last.points[i],priorAngle,priorSupport),b=transformed(next.points[i],angle,support);
                    peak=std::max(peak,length(a-b));
                }
            }
            last.points.swap(next.points);priorAngle=angle;priorSupport=support;
        }
        rig.pose(settled,0,0,0,1);assert(settled.points.size()==last.points.size());
        for(std::size_t i=0;i<last.points.size();i++){
            const auto a=last.points[i],b=settled.points[i];assert(length({a.x-b.x,a.y-b.y,a.z-b.z})<1e-6);
        }
        std::cout<<"Role "<<role<<": "<<frames+1<<" collapse samples; peak vertex step "<<peak<<".\n";
        assert(peak<.13);
    }
    double prior=dread::jawAngle(0),peak=0;
    for(int i=1;i<=int(std::ceil(dread::FeedDuration*240));i++){
        double a=dread::jawAngle(i/240.);peak=std::max(peak,std::abs(a-prior));prior=a;
    }
    assert(peak<.075 && prior==0);
    std::cout<<"Jaw: 240 Hz, peak angular step "<<peak<<" radians.\n";
}
