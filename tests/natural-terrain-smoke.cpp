#include "../src/natural-world.hpp"
#include "../src/terrain-piece.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

int main(){
    constexpr std::uint64_t seed=62108432;
    assert(natural::starterArea(0,-7));
    assert(!natural::starterArea(90,90));
    assert(std::abs(natural::starterHeight(0.5,-7,seed)-29)<1e-8);
    for(int z=-80;z<=80;z+=7)for(int x=-80;x<=80;x+=9){
        const auto h=natural::frontierHeight(x,z,seed);
        assert(h>=9 && h<=60 && std::isfinite(h));
        assert(h==natural::frontierHeight(x,z,seed));
    }
    bool different=false;
    for(int x=100;x<150;x+=4)different|=
        natural::frontierHeight(x,240,seed)!=natural::frontierHeight(x,240,seed+1);
    assert(different);
    for(bool floor:{false,true})for(bool crystal:{false,true}){
        TerrainStone stone(0x1234567u,crystal,floor);
        assert(std::abs(stone.crown.y-0.83)<0.12);
        int faces=0;
        stone.triangles([&](Vec a,Vec b,Vec c){
            ++faces;
            assert(length(cross(b-a,c-a))>1e-6);
            for(Vec p:{a,b,c}){
                assert(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z));
                assert(p.x>=-1e-5 && p.x<=1.00001 && p.y>=-1e-5 && p.y<=1.00001 && p.z>=-1e-5 && p.z<=1.00001);
            }
        },true);
        assert(faces>=40);
    }
    std::cout<<"PASS: starter clearing, seeded frontier, boulder topology and bounds\n";
}
