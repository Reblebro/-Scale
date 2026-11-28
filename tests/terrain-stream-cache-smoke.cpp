// c++ -std=c++20 -O2 -pthread tests/terrain-stream-cache-smoke.cpp src/world.cpp -o /tmp/scale-stream && /tmp/scale-stream
#include "../src/world.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
int main(){
    namespace fs=std::filesystem;
    using Clock=std::chrono::steady_clock;
    const auto root=fs::temp_directory_path()/ ("scale-stream-"+std::to_string(Clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    for(int z=-4;z<=4;++z){std::ofstream f(root/("row-"+std::to_string(z)+".bin"),std::ios::binary);f.seekp(128LL*32*32*64-1);f.put(0);}
    {
        World w(root),sync(root);
        for(auto* p:{&w,&sync}){p->setSeed(62108432);p->sculptedTerrain=true;}
        w.streamInteractive({0,29,0},2);
        assert(w.tileReady(1,1) && w.tileReady(-1,-1));
        auto start=Clock::now();
        // The first missing ring-2 tile is dispatched to a worker.
        while(!w.tileReady(-2,-2) && Clock::now()-start<std::chrono::seconds(20)){
            std::this_thread::sleep_for(std::chrono::milliseconds(5));w.streamInteractive({0,29,0},2);
        }
        assert(w.tileReady(-2,-2));
        assert(w.require(-2,-2)==sync.require(-2,-2));
        for(int z=-64;z<-32;z+=3)for(int x=-64;x<-32;x+=3){
            assert(w.roof({x,0,z})->elevation==sync.roof({x,0,z})->elevation);
            for(int y=24;y<ForestWorldHeight-1;y+=2)assert(w.naturalTree({x,y,z})==sync.naturalTree({x,y,z}));
        }
        w.require(1,1);
        Cell wood{};bool found=false;
        for(int z=32;z<64 && !found;++z)for(int x=32;x<64 && !found;++x)for(int y=28;y<ForestWorldHeight-1;++y){
            const Cell p{x,y,z};if(w.uneditedTreeMaterial(p)==Timber){wood=p;found=true;break;}
        }
        assert(found);
        const auto old=w.treeVersion({1,0,1});
        const Cell soil{32,2,32};assert(!w.naturalTree(soil));
        w.apply({0,soil,Air});
        assert(w.treeVersion({1,0,1})==old && w.meshVersion({1,0,1})>old);
        w.apply({0,wood,Air});
        assert(w.treeVersion({1,0,1})==w.revision() && w.uneditedTreeMaterial(wood)==Air);
        const Bounds fine{{128,16,128},{132,20,132}};
        const auto version=w.detailVersion(fine,1);
        w.apply({0,{63,6,63},Air});assert(w.detailVersion(fine,1)==version);
        w.apply({0,{32,4,32},Air});assert(w.detailVersion(fine,1)==w.revision());
    }
    fs::remove_all(root);
    std::cout<<"PASS: background tile matches synchronous world, tree-only dirty tracking, local fine cache invalidation\n";
}
