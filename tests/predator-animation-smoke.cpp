#include "../src/deer-art.hpp"
#include "../src/atmosphere.hpp"
#include "../src/goblin-rig.hpp"
#include "../src/settlement.hpp"
#include "../src/witness-horror.hpp"
#include <cassert>
#include <iostream>
#include <tuple>

int main(){
    Geometry head,skull,jaw;witnessHead(head);witnessHead(skull,&jaw);
    assert(skull.points.size()+jaw.points.size()==head.points.size());
    assert(jaw.points.size()>6000);
    auto sorted=[](const Geometry& g){std::vector<std::tuple<float,float,float,float>> out;
        for(auto& v:g.points)out.emplace_back(v.x,v.y,v.z,v.id);
        std::sort(out.begin(),out.end());return out;};
    Geometry combined=skull;combined.points.insert(combined.points.end(),jaw.points.begin(),jaw.points.end());
    assert(sorted(combined)==sorted(head));
    double minAngle=0,maxAngle=0;
    for(int i=0;i<int(dread::FeedDuration*100);i++){double a=dread::jawAngle(i*.01);minAngle=std::min(minAngle,a);maxAngle=std::max(maxAngle,a);}
    assert(minAngle<-.22 && maxAngle>.12 && dread::jawAngle(0)==0 && dread::jawAngle(dread::FeedDuration-.01)==0);
    const auto validate=[](const Geometry& g){assert(!g.points.empty() && g.points.size()%3==0);
        for(auto& v:g.points){assert(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z));assert(std::abs(length({v.nx,v.ny,v.nz})-1)<.003);}};
    Geometry idle;woodland::deer(idle,-1);validate(idle);
    std::size_t count=idle.points.size();assert(count>18000);
    for(int i=0;i<16;i++){Geometry pose;woodland::deer(pose,2*Pi*i/16);validate(pose);assert(pose.points.size()==count);}
    settlement::GoblinRig rig;rig.build(true);assert(rig.jointCount()==53);
    for(int i=0;i<7;i++){
        const double t=i/6.,a=dread::fallAngle(t);
        Geometry deer,goblin;woodland::deer(deer,-1,dread::smooth(t));rig.pose(goblin,0,0,0,dread::smooth(t));
        for(auto* g:{&deer,&goblin}){validate(*g);double support=dread::supportHeight(g->points,a),minY=1e9;
            for(auto& v:g->points)minY=std::min(minY,support+v.y*std::cos(a)+v.z*std::sin(a));
            assert(std::abs(minY)<1e-8);}
    }
    assert(settlement::GuardScale==1.45);
    std::cout<<"Deer: "<<count/3<<" triangles; 16 gait phases, 7 collapse poses per species; rest skull exactly preserved; "<<jaw.points.size()/3<<" moving jaw triangles.\n";
}
