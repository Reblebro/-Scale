#include "../src/world.hpp"
#include "../src/natural-world.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <vector>

int main(){
    constexpr std::uint64_t seed=62108432;
    int rooted=0,elders=0,withCanopies=0;
    for(int cz=-15;cz<15;++cz)for(int cx=-15;cx<15;++cx){
        const auto tree=natural::treeAtGrid(cx,cz,seed);
        if(!tree)continue;
        assert(tree->base>=24 && tree->base+tree->height+5<=ForestWorldHeight-2);
        assert(std::hypot(tree->x-0.5,tree->z+7.)>=35);
        assert(natural::treeMatter(*tree,tree->x,tree->base+2,tree->z)==2);
        int leafCount=0,woodCount=0;
        const int r=int(tree->radius*1.6)+1;
        for(int z=tree->z-r;z<=tree->z+r;++z)
            for(int x=tree->x-r;x<=tree->x+r;++x)
                for(int y=tree->base;y<=std::min(ForestWorldHeight-2,tree->base+tree->height+6);++y){
                    const int material=natural::treeMatter(*tree,x,y,z);
                    leafCount+=(material==1);woodCount+=(material==2);
                }
        assert(leafCount>150 && woodCount>25);
        ++rooted;elders+=tree->elder;withCanopies+=leafCount>500;
    }
    assert(rooted>100 && elders>1 && withCanopies>50);

    // Generation at either side of a streamed tile border must use exactly the
    // same tree descriptions, including crowns extending across that border.
    for(int tz=-2;tz<=2;++tz)for(int tx=-2;tx<=2;++tx){
        const auto a=natural::treesNearTile(tx,tz,seed);
        const auto b=natural::treesNearTile(tx+1,tz,seed);
        for(int z=tz*32;z<tz*32+32;++z)for(int x=tx*32+29;x<=tx*32+34;++x){
            std::set<std::pair<int,int>> left,right;
            for(const auto& t:a)if(natural::treeMatter(t,x,48,z))left.insert({t.x,t.z});
            for(const auto& t:b)if(natural::treeMatter(t,x,48,z))right.insert({t.x,t.z});
            assert(left==right);
        }
    }

    int villageCount=0;bool foundArchitecture=false;
    for(int sz=-5;sz<=5;++sz)for(int sx=-5;sx<=5;++sx){
        const auto tree=natural::elfVillageAtSector(sx,sz,seed);
        if(!tree)continue;
        ++villageCount;
        assert(tree->village);
        assert(tree->base+tree->height+5<=63);
        assert(natural::treeMatter(*tree,tree->x,tree->base+2,tree->z)==2);
        std::set<int> architecture;
        for(int z=tree->z-7;z<=tree->z+7;++z)
          for(int x=tree->x-7;x<=tree->x+7;++x)
            for(int y=tree->base;y<=62;++y)
                architecture.insert(natural::villageMatter(*tree,x,y,z));
        if(architecture.count(1)&&architecture.count(2)&&architecture.count(3)&&architecture.count(4))
            foundArchitecture=true;
    }
    assert(villageCount>0 && villageCount<25 && foundArchitecture);

    // Exercise real World::require on two streamed tiles. Prior authored data
    // has rogue stone/ore on the surface; no such patch may survive generation.
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/"scale-mountain-forest-smoke";
    fs::create_directories(root);
    {
        constexpr long long tileSize=32*32*64;
        std::ofstream f(root/"row-0.bin",std::ios::binary|std::ios::trunc);
        f.seekp(128*tileSize-1);char zero=0;f.write(&zero,1);
        for(int tx:{0,1})for(int x=0;x<32;++x)for(int z=0;z<32;++z){
            const long long offset=(tx+64)*tileSize+((z*32+x)*64+30);
            f.seekp(offset);char c=char((x+z)%2?CopperOre:Stone);f.write(&c,1);
        }
    }
    {
        World world(root);world.setSeed(seed);world.sculptedTerrain=true;
        world.require(0,0);world.require(1,0);
        int foliage=0,trunks=0,joinedStemCaps=0;
        for(int z=0;z<32;++z)for(int x=0;x<64;++x){
            const auto* roof=world.roof({x,0,z});assert(roof);
            const int low=std::clamp(int(std::floor(roof->low())),1,62);
            const int high=std::clamp(int(std::ceil(roof->high()))-1,low,62);
            for(int y=low;y<=high;++y){
                const int surface=world.sample(0,{x,y,z}).id;
                assert(surface==Grass || surface==Dirt || surface==Sand || surface==Timber);
                const Cell rootCell{x,y,z};
                if(world.treeThroughRoof(rootCell)){
                    assert(surface==Timber && world.naturalTree(rootCell));
                    assert(world.leafRoof({rootCell,0,Timber})==nullptr);
                    assert(world.sample(1,rootCell*4+Cell{2,3,2}).id==Timber);
                    ++joinedStemCaps;
                }
            }
            for(int y=high+1;y<ForestWorldHeight;++y){
                const int m=world.sample(0,{x,y,z}).id;
                assert(m==Air || m==Leaves || m==Timber || m==Plank || m==RoofTile || m==Lantern);
                foliage+=m==Leaves;trunks+=m==Timber;
            }
        }
        assert(foliage>100 && trunks>5 && joinedStemCaps>5);
    }
    // Load a REAL generated village from an actual streamed tile, rather than
    // merely checking the material math for its platforms and dwellings.
    {
        const auto village=natural::elfVillageAtSector(0,-2,seed);
        assert(village && village->x==110 && village->z==-280);
        constexpr long long tileSize=32*32*64;
        std::ofstream f(root/"row--9.bin",std::ios::binary|std::ios::trunc);
        f.seekp(128*tileSize-1);char zero=0;f.write(&zero,1);f.close();
        World world(root);world.setSeed(seed);world.sculptedTerrain=true;
        world.require(3,-9);
        const int deck=village->base+village->height-10;
        assert(world.sample(0,{village->x+4,deck,village->z}).id==Plank);
        assert(world.sample(0,{village->x+3,deck+4,village->z}).id==RoofTile);
        assert(world.sample(0,{village->x+5,deck+2,village->z}).id==Lantern);
    }
    fs::remove_all(root);
    std::cout<<"PASS mountain forests: "<<rooted<<" rooted trees, "<<elders
             <<" elders, "<<villageCount<<" rare village candidates; crowns,"
             <<" tile seams, architecture, and clean grassy surfaces\n";
}
