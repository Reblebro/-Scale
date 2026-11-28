// c++ -std=c++20 -O2 -pthread tests/highland-terrain-smoke.cpp src/world.cpp -o /tmp/highland-test
#include "../src/world.hpp"
#include "../src/natural-world.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
int main(){
    namespace fs=std::filesystem;
    const auto dir=fs::temp_directory_path()/("scale-highland-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    struct Cleanup {fs::path path; ~Cleanup(){std::error_code e;fs::remove_all(path,e);}} cleanup{dir};
    const std::uint64_t seed=62108432;
    int highTrees=0,aboveOldLimit=0,shortest=100;
    std::set<int> crowns;
    std::optional<natural::ForestTree> high;
    for(int z=-40;z<=40;++z)for(int x=-40;x<=40;++x){
        auto t=natural::treeAtGrid(x,z,seed);
        if(!t)continue;
        assert(t->height>=19 && t->base+t->height+5<ForestWorldHeight);
        if(t->base<43)continue;
        ++highTrees;shortest=std::min(shortest,t->height);crowns.insert(t->base+t->height);
        if(t->base+t->height>ArchiveHeight)++aboveOldLimit;
        if(!high || t->base>high->base)high=t;
    }
    assert(high && highTrees>20 && aboveOldLimit==highTrees && crowns.size()>8);
    const int tx=divFloor(high->x,32),tz=divFloor(high->z,32);
    const auto rowBytes=128LL*32*32*ArchiveHeight;
    for(int z=tz-1;z<=tz+1;++z){
        std::ofstream f(dir/("row-"+std::to_string(z)+".bin"),std::ios::binary);
        f.seekp(rowBytes-1);f.put(0);
        // Different legacy columns are deliberately distinguishable after expansion.
        if(z==tz){for(int lx:{0,1,31}){
            const auto index=(tx+64LL)*32*32*ArchiveHeight+lx*ArchiveHeight+63;
            f.seekp(index);f.put(char(lx==1?CopperOre:Stone));
        }}
    }
    World legacy(dir);assert(legacy.heightLimit()==ArchiveHeight);
    legacy.require(tx,tz);
    assert(legacy.sample(0,{tx*32,63,tz*32}).id==Stone);
    assert(legacy.sample(0,{tx*32+1,63,tz*32}).id==CopperOre);
    assert(legacy.sample(0,{tx*32+31,63,tz*32}).id==Stone);
    assert(!legacy.sample(0,{tx*32+1,64,tz*32}).amount);
    assert(!legacy.apply({0,{tx*32+1,70,tz*32},Timber}));
    World modern(dir);modern.setSeed(seed);modern.sculptedTerrain=true;
    assert(modern.heightLimit()==ForestWorldHeight);
    modern.stream({high->x,high->base,high->z},1);
    int elevatedWood=0,elevatedLeaves=0;
    Cell upperWood{};
    for(int z=tz*32;z<(tz+1)*32;++z)for(int x=tx*32;x<(tx+1)*32;++x){
        for(int y=ArchiveHeight;y<ForestWorldHeight;++y){
            const Cell p{x,y,z};const auto id=modern.uneditedTreeMaterial(p);
            if(id==Timber){++elevatedWood;upperWood=p;}
            if(id==Leaves)++elevatedLeaves;
        }
    }
    assert(elevatedWood>30 && elevatedLeaves>100);
    // The actual bole passes the old ceiling. Collision and ray queries see it.
    const int centerY=std::min(high->base+high->height-3,ArchiveHeight+2);
    assert(centerY>=ArchiveHeight);
    const Vec bole=natural::stemAt(*high,centerY);
    const Cell boleCell{high->x+int(std::round(bole.x)),centerY,high->z+int(std::round(bole.z))};
    assert(modern.sample(0,boleCell).id==Timber);
    const auto hit=modern.ray({high->x+.5,double(ForestWorldHeight-1),high->z+.5},{0,-1,0},0,ForestWorldHeight);
    assert(hit && hit->cell.y>=ArchiveHeight);
    const auto oldVersion=modern.treeVersion({tx,0,tz});
    assert(modern.apply({0,upperWood,Air})==-FullVolume);
    assert(!modern.sample(0,upperWood).amount && modern.treeVersion({tx,0,tz})>oldVersion);
    assert(modern.apply({0,upperWood,Timber})==FullVolume);
    const Cell sky{tx*32,ForestWorldHeight-1,tz*32};
    assert(modern.apply({0,sky,Stone})==FullVolume);
    assert(modern.sample(0,sky).id==Stone);
    assert(!modern.apply({0,sky+Cell{0,1,0},Stone}));
    World replay(dir);replay.setSeed(seed);replay.sculptedTerrain=true;
    for(const auto& [tile,edits]:modern.saveChunks())for(const auto& edit:edits)replay.apply(edit);
    assert(replay.sample(0,sky).id==Stone && replay.sample(0,upperWood).id==Timber);
    for(int z=tz*32;z<(tz+1)*32;++z){
        const auto a=modern.roof({(tx+1)*32-1,0,z}),b=modern.roof({(tx+1)*32,0,z});
        assert(a && b && a->elevation[1]==b->elevation[0] && a->elevation[2]==b->elevation[3]);
    }
    std::cout<<"PASS: "<<highTrees<<" highland trees above the old ceiling; shortest "<<shortest
             <<"m; "<<crowns.size()<<" crown elevations; "<<elevatedWood<<" upper timber and "<<elevatedLeaves
             <<" upper foliage voxels in one tile; legacy archive, collision, ray, editing and seams verified\n";
}
