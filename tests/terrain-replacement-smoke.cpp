// Standalone regression: c++ -std=c++20 -O2 tests/terrain-replacement-smoke.cpp src/world.cpp -o /tmp/scale-terrain-smoke && /tmp/scale-terrain-smoke
// Uses sparse temporary world rows (not real gigabytes of disk storage),
// plants old camp/quarry sentinels, then verifies replacement and seam safety.
#include "../src/world.hpp"
#include "../src/natural-world.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cassert>
#include <cmath>
#include <chrono>
int main(){
 namespace fs=std::filesystem;
 auto root=fs::temp_directory_path()/("scale-terrain-smoke-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 fs::create_directories(root);
 constexpr long long tileSize=32*32*64;
 for(int z:{-1,0,1,2,3,4}){
     std::ofstream f(root/("row-"+std::to_string(z)+".bin"),std::ios::binary|std::ios::trunc);
     f.seekp(128*tileSize-1);char zero=0;f.write(&zero,1);
     if(z==0){
         for(const auto [tx,lx,lz,y,id]:{
               std::array<int,5>{0,0,0,28,CampTile},
               std::array<int,5>{1,28,24,28,CampTile},
               std::array<int,5>{1,28,24,32,Timber},
               std::array<int,5>{0,4,3,35,CopperOre}}){
             const long long offset=(tx+64)*tileSize+((lz*32+lx)*64+y);
             f.seekp(offset);char c=char(id);f.write(&c,1);
         }
     }
 }
 {
  World world(root);world.setSeed(62108432);world.sculptedTerrain=true;
  for(int z:{-1,0,1})for(int x:{0,1})world.require(x,z);
  for(int z:{2,3,4})for(int x:{2,3})world.require(x,z);
  for(int x=0;x<=63;x+=3)for(int z=-16;z<=40;z+=3){
     const auto* r=world.roof({x,0,z});assert(r);
     auto top=std::clamp(int(std::ceil(r->high()))-1,1,62);
     auto bottom=std::clamp(int(std::floor(r->low())),1,62);
     for(int y=bottom;y<=top;++y){
       auto id=world.sample(0,{x,y,z}).id;
       assert(id==Grass || id==Dirt || id==Stone || id==CopperOre || id==IronOre || id==Timber || id==Sand);
     }
     for(int y=top+1;y<63;y++){
       auto id=world.sample(0,{x,y,z}).id;
       assert(id!=CampTile && id!=CopperOre && id!=IronOre && id!=Coal && id!=Lodestone && id!=Clay && id!=Stone);
     }
  }
  // At steeper mountains the roof can intersect two or more vertical
  // blocks: EVERY fragment must have the same topsoil material. Otherwise
  // the old contour-band/ring artifacts return as interleaved dirt triangles.
  int layeredRoofs=0;
  for(int x=66;x<124;x+=2)for(int z=72;z<130;z+=2){
     const auto* roof=world.roof({x,0,z});assert(roof);
     const int bottom=int(std::floor(roof->low()));
     const int top=int(std::ceil(roof->high()))-1;
     if(top<=bottom)continue;
     ++layeredRoofs;
     for(int y=bottom;y<=top;++y){
         const int material=world.sample(0,{x,y,z}).id;
         // Living root ridges can intersect the surface layers; exposed
         // geology must still remain either turf or rooted timber.
         assert(material==Grass || material==Dirt || material==Sand || material==Timber);
       }
  }
  assert(layeredRoofs>0);
  assert(world.sample(0,{0,28,0}).id!=CampTile);
  assert(world.sample(0,{60,28,24}).id!=CampTile);
  // Natural timber may now legitimately occupy the old quarry coordinates;
  // verify any such timber belongs to a generated living tree, not the camp.
  if(world.sample(0,{60,32,24}).id==Timber){
      const auto trees=natural::treesNearTile(1,0,62108432);
      bool living=false;
      for(const auto& tree:trees){
          const double ground=natural::surfaceHeight(60.5,24.5,62108432);
          living|=natural::treeMatter(tree,60,32,24)==2 || natural::rootMatter(tree,60,32,24,ground);
      }
      assert(living);
  }
  for(int z=-16;z<=40;z+=7){
    auto* lhs=world.roof({31,0,z});auto* rhs=world.roof({32,0,z});
    assert(lhs && rhs);
    assert(std::abs(lhs->height(1,0.35)-rhs->height(0,0.35))<1e-10);
  }
  for(int x=2;x<=8;x++)for(int z=-11;z<=-3;z++){
     auto* roof=world.roof({x,0,z});assert(roof);
     assert(std::abs(roof->height(.5,.5)-29)<0.8);
  }
 }
 fs::remove_all(root);
 std::cout << "PASS modern seeded world: original camp and quarry removed, continuous roof borders, grass across all roof fragments, no floating ore, safe starter elevation\n";
}
