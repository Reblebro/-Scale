#include "micro.hpp"
#include "edited-mesh.hpp"
#include "movement.hpp"
#include <iostream>
#include <random>
#include <stdexcept>

namespace {
void check(bool result,const char* label){
    if(!result)throw std::runtime_error(std::string("Terrain stress: ")+label);
}
bool before(Vec min,Vec max) {return min.x<max.x && min.y<max.y && min.z<max.z;}
int truthAt(Cell cell,const std::vector<Edit>& edits) {
    for(auto it=edits.rbegin();it!=edits.rend();++it){
        if(parent(cell,subdivisions(FineDepth-it->level))==it->position) return it->id;
    }
    return Air;
}
void shovelTests(){
    const auto field=[](Cell p){return p.y<10?int(Dirt):int(Air);};
    const auto any=[](Leaf){return true;};
    const auto height=[](const World& w,double x,double z,int tier=0){
        const auto hit=w.ray({x,11.2,z},{0,-1,0},tier,3);
        check(bool(hit),"shovel surface remains targetable");
        return 11.2-hit->distance;
    };
    World w{std::function<int(Cell)>(field)};
    const Vec aim{-32.0,10,0.0};
    const auto dug=w.shovel(aim,0,Dirt,false,FullBlock,any);
    check(dug>0 && dug<FullBlock,"a shovel gathers a fraction of a block");
    const double bottom=height(w,aim.x,aim.z);
    check(bottom<9.65 && bottom>9.5,"one stroke makes a shallow bowl");
    check(height(w,aim.x+0.35,0)>bottom+0.1 && height(w,aim.x+0.85,0)==10,
          "the scoop has sloping sides and an untouched rim");
    const double x=aim.x+0.14,z=0.08;
    const double bed=height(w,x,z);
    check(std::abs(bed-height(w,x,z,3))<1e-7,"fine and coarse targeting see the same facets");
    Geometry geometry;
    terrain::editedRoot(geometry,w,floorCell({x,bed,z}),{},Color{1,1,1},false);
    double visible=-1;
    for(std::size_t i=0;i<geometry.points.size();i+=3){
        const auto a=geometry.points[i],b=geometry.points[i+1],c=geometry.points[i+2];
        if(a.ny<=0)continue;
        const double det=(b.z-c.z)*(a.x-c.x)+(c.x-b.x)*(a.z-c.z);
        if(std::abs(det)<1e-12)continue;
        const double u=((b.z-c.z)*(x-c.x)+(c.x-b.x)*(z-c.z))/det;
        const double v=((c.z-a.z)*(x-c.x)+(a.x-c.x)*(z-c.z))/det;
        if(u>=0 && v>=0 && u+v<=1)visible=std::max(visible,u*a.y+v*b.y+(1-u-v)*c.y);
    }
    check(std::abs(visible-bed)<1e-6,"the submitted mesh and mining ray intersect at the same height");
    const Vec start{x,11,z};
    const double landing=start.y+w.sweep(start,0.001,1,-3);
    check(std::abs(landing-bed)<0.001,"player contact follows the rendered dirt slope");
    check(std::abs(height(w,aim.x-1e-5,0.12)-height(w,aim.x+1e-5,0.12))<0.0001,
          "scoop joins across a negative chunk boundary");
    check(w.meshVersion({-1,0,0}) && w.meshVersion({-2,0,0}),"both edited chunks rebuild");
    const Vec pile{-30.2,10,2.3};
    auto spent=w.shovel(pile,0,Dirt,true,dug/2,any);
    check(spent>0 && spent<=dug/2 && height(w,pile.x,pile.z)>10.08,"a partial inventory builds a mound");
    const auto taller=w.shovel({pile.x,height(w,pile.x,pile.z),pile.z},0,Dirt,true,dug,any);
    check(taller>0 && height(w,pile.x,pile.z)>10.25,"successive deposits stack");
    const auto prior=w.revision();
    check(!w.shovel(aim,0,Dirt,false,FullBlock,[](Leaf){return false;}) && prior==w.revision(),
          "protected supports remain intact");
    World hard{std::function<int(Cell)>([](Cell p){return p.y<10?int(Stone):int(Air);})};
    check(hard.shovel(aim,0,Dirt,false,FullBlock,any)==0 && !hard.revision(),"shovel leaves stone intact");
    check(hard.shovel(aim,0,Dirt,true,dug,any)>0 && height(hard,aim.x,aim.z)>10.25 &&
          hard.sample(0,{-32,9,0}).id==Stone,"stone can support a dirt pile");
    for(int id=Grass;id<=Quartz;++id){
        World blocks{std::function<int(Cell)>([id](Cell p){return p.y<10?id:int(Air);})};
        const int item=harvestMaterial(id);
        const auto scoop=blocks.shovel(aim,0,item,false,FullBlock,any);
        check(scoop>0 && height(blocks,aim.x,aim.z)<9.7,"each mineable material accepts a bowl-shaped cut");
        check(blocks.shovel(pile,0,item,true,scoop,any)>0 && height(blocks,pile.x,pile.z)>10.18,
              "each harvested material builds a mound");
    }
    World scraps{std::function<int(Cell)>([](Cell){return int(Air);})};
    const Cell tip{4,40,4};
    scraps.apply({1,tip,Stone,SoilHeights{18,-200,-200,-200}});
    const Vec corner=tip.v()/4+Vec{0.001,0.004,0.001};
    check(scraps.ray(corner+Vec{0,0.2,0},{0,-1,0},1,0.4).has_value(),"old sliver is targetable at its corner");
    scraps.shovel(corner,0,Stone,false,FullBlock,any);
    check(!scraps.partition(1,tip).material,"a stroke clears old thin strands that miss the column center");
    const Cell fine{16,160,16};
    scraps.apply({2,fine,IronOre});
    check(scraps.shovel(fine.v()/16+Vec{0.03,0.06,0.03},0,IronOre,false,FullBlock,any)>0 &&
          !scraps.partition(2,fine).material,"coarse digging reaches refined remnants");
    World smallest{std::function<int(Cell)>(field)};
    check(smallest.shovel({0.5,10,0.5},FineDepth,Dirt,false,FullBlock,any)>0,
          "a shovel stroke works at the finest terrain tier");
    World crystal{std::function<int(Cell)>([](Cell p){return p.y<10?int(Stone):int(Air);})};
    crystal.sculptedTerrain=true;
    crystal.apply({0,{0,10,0},Quartz});
    const Vec above{0.48,12,0.48};
    const auto crown=crystal.ray(above,{0,-1,0},0,3);
    check(crown && crown->id==Quartz,"loose crystal exposes its crown");
    const Vec strike=above-Vec{0,crown->distance,0};
    check(crystal.shovel(strike,0,Quartz,false,FullBlock,any,crown->surfaceNormal)>0,
          "shovel fractures an isolated faceted crystal");
    check(!crystal.overlaps({0.015,10.91,0.015},0.006),"fracturing a crystal preserves its empty outer corner");
    World restored{std::function<int(Cell)>(field)};
    for(const auto& [key,edits]:w.saveChunks())for(const auto& edit:edits)restored.apply(edit);
    for(const auto pt:{aim,pile,Vec{x,10,z}})
        check(std::abs(height(w,pt.x,pt.z)-height(restored,pt.x,pt.z))<1e-8,"chunk replay preserves shovel surfaces");
    World slope{std::function<int(Cell)>([](Cell p){return p.y<11?int(Dirt):int(Air);})};
    slope.defineRoof({0,0,0},TerrainRoof{{10.1,10.6,10.6,10.1}});
    const Vec onSlope{0.5,10.35,0.5};
    check(slope.shovel(onSlope,1,Dirt,false,FullBlock,any)>0 &&
          height(slope,0.5,0.5)<10.30 && std::abs(height(slope,0.8,0.5)-10.5)<1e-6,
          "small strokes preserve the surrounding natural slope");
    std::cout<<"Verified shovel mesh contacts, seams, inventory, building on stone, and saved edits.\n";
}

}

void terrainStressTests(const std::filesystem::path& data) {
    shovelTests();
    // All edits stay in memory. Using real row files also exercises the negative
    // chunk division and the same lazy world data that the released build reads.
    World world(data);
    world.stream({0,59,0},2);
    constexpr int lattice=subdivisions(FineDepth);
    std::mt19937 random(140924);
    const auto choose=[&](int count){return int(random()%std::uint32_t(count));};
    const std::array<Cell,4> roots={{{-33,59,-32},{-32,59,-32},{-1,59,0},{0,59,0}}};
    const std::array<int,5> material={Air,Stone,Dirt,Timber,Clay};
    std::vector<Edit> record;
    for(Cell root:roots)check(world.sample(0,root).id==Air,"mixed-resolution fixture is initially air");
    for(int i=0;i<1900;i++){
        const auto root=roots[choose(roots.size())];
        const int level=choose(FineDepth+1);
        const int n=subdivisions(level);
        const auto key=root*n+Cell{choose(n),choose(n),choose(n)};
        const Edit edit{level,key,material[choose(material.size())]};
        world.apply(edit);
        record.push_back(edit);
        for(int no=0;no<8;no++){
            const Cell cell=root*lattice+Cell{choose(lattice),choose(lattice),choose(lattice)};
            check(world.sample(FineDepth,cell).id==truthAt(cell,record),"sparse hierarchy equals direct edit history");
        }
    }

    // Compact output is grouped by the published 32 x 32 chunks. Read every
    // surviving event into a fresh world and verify that no fine leaf changes.
    const auto packed=world.saveChunks();
    World restored(data);
    restored.stream({0,59,0},2);
    std::size_t persisted=0;
    for(const auto& [chunk,list]:packed){
        check(chunk.y==0,"chunk coordinates are horizontal");
        for(const auto& e:list){
            const auto root=parent(e.position,subdivisions(e.level));
            check(parent(Cell{root.x,0,root.z},32)==chunk,"persisted record belongs to its declared chunk");
            restored.apply(e);
            persisted++;
        }
    }
    check(persisted<record.size()/2,"superseded edits compact without changing the terrain");
    for(const auto& root:roots){
        for(int i=0;i<4000;i++){
            const auto probe=root*lattice+Cell{choose(lattice),choose(lattice),choose(lattice)};
            check(restored.sample(FineDepth,probe).id==world.sample(FineDepth,probe).id,
                "chunk serialization keeps the same material on every sampled leaf");
        }
        Composition composition{};
        const Box box{root.v(),root.v()+Vec{1,1,1}};
        restored.visitMatter(box,[&](Leaf leaf){
            const auto b=leaf.bounds();
            check(before(b.lo,b.hi) && box.intersects(b),"no degenerate geometry or cells outside the root");
            composition[leaf.material]+=volume(leaf.level);
            return true;
        });
        check(composition==world.composition(0,root),"leaf volumes add up to exact coarse composition");
    }

    // An aperture cut through a meter cube. Fast sweeps from either side must
    // stop a large hull, while a tiny hull can traverse the actual opening.
    const Cell barrier{9,59,1};
    check(world.sample(0,barrier).id==Air,"aperture test fixture is empty");
    world.apply({0,barrier,Stone});
    for(int x=0;x<4;x++)world.apply({1,barrier*4+Cell{x,2,2},Air});
    for(int i=0;i<700;i++){
        const double body=0.02+choose(780)/1000.0;
        const double direction=i%2?-1:1;
        const Vec feet{direction>0?8.30:10.70,59.52,1.63};
        const auto box=World::playerBox(feet,body);
        const bool clear=box.hi.y<=59.75 && box.lo.y>=59.50 && box.lo.z>=1.50 && box.hi.z<=1.75;
        check(!world.overlaps(feet,body),"stress starts in free space");
        const auto want=direction*2.55;
        const auto moved=world.sweep(feet,body,0,want);
        if(clear)check(std::abs(moved-want)<1e-8,"sweeps cross a real opening of sufficient size");
        else {
            const double face=direction>0?9-box.hi.x:10-box.lo.x;
            check(std::abs(moved-face)<0.00001,"fast sweeps stop exactly at the first solid face");
        }
        check(!world.overlaps(feet+Vec{moved,0,0},body),"no collision penetration after the sweep");
    }
    world.apply({0,barrier,Water});
    check(world.sweep({8.3,59.5,1.6},0.3,0,3.0)>2.99,"water does not form a solid movement barrier");

    const Cell materialMix{15,59,3};
    world.apply({0,materialMix,Stone});
    const Cell oneOfMany=materialMix*lattice+Cell{152,24,79};
    world.apply({FineDepth,oneOfMany,Dirt});
    check(world.sample(0,materialMix).amount==FullVolume &&
        world.sample(FineDepth,oneOfMany).id==Dirt,"mixed full cube retains its fine material");
    world.apply({0,materialMix,Stone});
    check(world.sample(FineDepth,oneOfMany).id==Stone && !world.editedChildren(materialMix),
        "an explicit full edit of the same coarse type resets superseded child material");

    const Cell isolated{6,58,3};
    const Cell thin=isolated*lattice+Cell{lattice/2,lattice/4,lattice/2};
    world.apply({FineDepth,thin,Stone});
    const Box shelf=Leaf{thin,FineDepth,Stone}.bounds();
    const Vec falling{(shelf.lo.x+shelf.hi.x)*0.5,60.2,(shelf.lo.z+shelf.hi.z)*0.5};
    const auto down=world.sweep(falling,0.003,1,-12);
    check(std::abs((falling.y+down)-shelf.hi.y)<0.00001,
        "a long fall lands on the smallest explicitly placed shelf");
    check(!world.overlaps(falling+Vec{0,down,0},0.003),"thin shelf landing leaves the player clear");
    micro::Area chamber(0,3);
    const Vec foot{24.5,5.001,8.2};
    for(int y=5;y<9;y++)chamber.set({24,y,10},Stone);
    const double stop=chamber.sweep(foot,0.2,2,30);
    check(stop>1.70 && stop<1.8 && !chamber.overlaps(foot+Vec{0,0,stop},0.2),
        "long movement cannot cross inner-world terrain");
    World stairs(std::function<int(Cell)>([](Cell p){return p.y<4?int(Stone):int(Air);}));
    for(int x=8;x<12;x++)for(int z=0;z<4;z++)stairs.apply({1,{x,16,z},Stone});
    const auto sweep=[&](Vec pos,double size,int axis,double direction){return stairs.sweep(pos,size,axis,direction);};
    constexpr double hz=1.0/60;
    Vec player{1.65,4.0000001,0.52};
    bool prior=false,grounded=true;
    mobility::Vertical vertical;
    mobility::advance(player,0.5,{0.63,0,0},hz,0.4,0.5,false,false,prior,grounded,vertical,sweep);
    check(player.x>2.24 && std::abs(player.y-4.25)<1e-4 && !stairs.overlaps(player,0.5),
        "automatic step lands on a real tread without springing over it");
    constexpr Vec landing{1.3,4.069,0.52};
    player=landing;grounded=prior=false;vertical.velocity=-5;
    for(int i=0;i<14;i++)
        mobility::advance(player,0.5,{},hz,0.4,0.5,false,i==0,prior,grounded,vertical,sweep);
    check(player.y>landing.y && !stairs.overlaps(player,0.5),"a jump pressed just before landing is buffered");
    std::cout<<"Stress-tested 1,900 hierarchical edits, chunk replay, 16,000 leaf samples, 700 fast sweeps, stepping, and jump buffering.\n";
}
