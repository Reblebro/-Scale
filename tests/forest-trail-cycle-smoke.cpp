#include "../src/natural-world.hpp"
#include "../src/daylight.hpp"
#include "../src/trail-world.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <unordered_set>

int main(){
    constexpr std::uint64_t seed=62108432;
    // Enumerate actual planted sites, not potential grid slots. Require a
    // denser forest while respecting reserved walkways and minimum spacing.
    std::vector<natural::ForestTree> trees;
    int competed=0;
    for(int z=-12;z<=12;++z)for(int x=-12;x<=12;++x){
        const auto t=natural::treeAtGrid(x,z,seed);if(!t)continue;
        const auto raw=natural::rawTreeAtGrid(x,z,seed);assert(raw);
        assert(!natural::trailReserved(t->x+.5,t->z+.5,seed));
        for(const auto& p:trees)assert(std::hypot(p.x-t->x,p.z-t->z)>=8);
        for(std::size_t i=0;i<t->boughs.size();++i)if(t->boughs[i].rx<raw->boughs[i].rx*.99)++competed;
        trees.push_back(*t);
    }
    assert(trees.size()>180 && competed>20);
    // Every tree whose bole belongs to a tile must be found by the tile scan.
    for(const auto& t:trees){
        const auto tile=natural::treesNearTile(divFloor(t.x,32),divFloor(t.z,32),seed);
        assert(std::any_of(tile.begin(),tile.end(),[&](const auto& p){return p.x==t.x && p.z==t.z;}));
    }
    // Lanterns are owned exactly once across tile edges. On uninterrupted
    // sections the pools of light must overlap at walking height.
    std::map<int,std::map<int,Vec>> lanes;
    int lanterns=0;
    for(int tz=-5;tz<=5;++tz)for(int tx=-5;tx<=5;++tx){
        for(Vec p:natural::trailLanterns(tx,tz,seed)){
            assert(p.x>=tx*32 && p.x<(tx+1)*32 && p.z>=tz*32 && p.z<(tz+1)*32);
            assert(lanes[natural::trailLane(p.x)].emplace(int(p.z),p).second);
            assert(p.y>=25 && p.y<=59);++lanterns;
        }
    }
    int continuous=0;double maxGap=0;
    for(const auto& [lane,sites]:lanes)for(const auto& [z,p]:sites){
        auto next=sites.find(z+natural::TrailInterval);if(next==sites.end())continue;
        const auto q=next->second;
        assert((p.x-natural::trailX(lane,p.z,seed))*(q.x-natural::trailX(lane,q.z,seed))<0);
        maxGap=std::max(maxGap,length(p-q));
        const double midZ=(p.z+q.z)*.5;
        const double midX=natural::trailX(lane,midZ,seed);
        const Vec mid{midX,natural::surfaceHeight(midX,midZ,seed),midZ};
        assert(length(p+Vec{0,1.90,0}-mid)<natural::TrailLightRadius);
        assert(length(q+Vec{0,1.90,0}-mid)<natural::TrailLightRadius);++continuous;
    }
    assert(lanterns>50 && continuous>30);
    using namespace solarcycle;
    assert(at(0).night && !at(CycleSeconds*.5).night);
    assert(at(0).day==0 && at(CycleSeconds*.5).day==1);
    assert(length(at(InitialClock).sun-at(InitialClock+CycleSeconds).sun)<1e-10);
    assert(at(CycleSeconds*.25).twilight>.2);
    assert(shadowSun(301)==shadowSun(304.9));
    assert(shadowSun(301)!=shadowSun(306));
    // Run actual world generation and count occupied canopy columns.
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/"scale-forest-trail-cycle-smoke";
    fs::create_directories(root);
    for(int z:{1,2}){std::ofstream f(root/("row-"+std::to_string(z)+".bin"),std::ios::binary|std::ios::trunc);f.seekp(128LL*32*32*64-1);f.put(0);}
    int covered=0,columns=0,grounded=0,pathColumns=0;
    auto genStart=std::chrono::steady_clock::now();
    {
        World w(root);w.setSeed(seed);w.sculptedTerrain=true;
        for(int z:{1,2})for(int x:{0,1})w.require(x,z);
        for(int z:{1,2})for(int x:{0,1})for(const Vec p:natural::groundedTrail(w,x,z)){
            const Cell c=floorCell(p);const auto* roof=w.roof(c);assert(roof);
            assert(std::abs(roof->height(p.x-c.x,p.z-c.z)-p.y)<1e-6);
            assert(!w.overlaps(p+Vec{0,.18,0},.90,2.24/.90));++grounded;
        }
        for(int z=32;z<96;++z)for(int x=0;x<64;++x){
            const auto* roof=w.roof({x,0,z});assert(roof);++columns;
            if(natural::trailSurface(x+.5,z+.5,seed)){
                const Cell cap{x,int(std::ceil(roof->height(.5,.5)))-1,z};
                const int material=w.sample(0,cap).id;
                assert(material==Dirt || material==Timber);++pathColumns;
            }
            for(int y=int(std::ceil(roof->high()))+3;y<ForestWorldHeight-1;++y)
                if(w.sample(0,{x,y,z}).id==Leaves){++covered;break;}
        }
    }
    fs::remove_all(root);
    assert(covered>columns/3 && grounded>=3 && pathColumns>40);
    std::cout<<"PASS: "<<trees.size()<<" trees, "<<competed<<" competition-adjusted crowns, "<<lanterns
             <<" lanterns, "<<continuous<<" touching light pairs; largest adjacent gap "<<maxGap
             <<"m; "<<grounded<<" actual grounded posts; "<<covered<<"/"<<columns<<" canopy-covered columns; 4 tile generation seconds "
             <<std::chrono::duration<double>(std::chrono::steady_clock::now()-genStart).count()<<"\n";
}
