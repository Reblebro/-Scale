#include "../src/micro.hpp"
#include "../src/expedition.hpp"
#include "../src/arboreal-mesh.hpp"
#include <cassert>
#include <iostream>
#include <queue>

int main(){
    // Parent faces that cover the entire playable realm are split into four
    // exterior strips. No resulting triangle may cross the protected room.
    Geometry backdrop;
    for(int side=0;side<6;side++){
        Vec p{-100, -100,-100};p[side/2]=side%2?-190:10;
        micro::backdropFace(backdrop,p,200,side,Grass,{1,1,1});
    }
    assert(!backdrop.points.empty());
    const Box room{{-8,-6,-8},{micro::Width+8.,micro::Height+10.,micro::Depth+8.}};
    for(std::size_t i=0;i<backdrop.points.size();i+=3){
        Vec lo{1e9,1e9,1e9},hi{-1e9,-1e9,-1e9};
        for(int k=0;k<3;k++){const auto& v=backdrop.points[i+k];const Vec q{v.x,v.y,v.z};
            for(int a=0;a<3;a++){lo[a]=std::min(lo[a],q[a]);hi[a]=std::max(hi[a],q[a]);}}
        bool outside=false;for(int a=0;a<3;a++)outside|=hi[a]<=room.lo[a]+1e-5 || lo[a]>=room.hi[a]-1e-5;
        assert(outside);
    }
    std::size_t trees=0,triangles=0,groundTests=0,rooms=0;
    for(std::uint64_t seed:{1ULL,62108432ULL,987654321ULL})for(int family=0;family<2;family++)for(int level=0;level<micro::Count;level++){
        micro::Area area(family,level,seed,true);++rooms;
        // All entrances, exits and connecting paths remain usable.
        for(int z=3;z<=52;z++){
            Vec p{24.5,5.02,double(z)+.5};
            assert(!area.overlaps(p,1));
            const auto hit=area.ray(p,{0,-1,0},.2);
            assert(hit && hit->distance<.08);++groundTests;
        }
        expedition::Wildlife life;life.prepare(area);
        for(int i=0;i<expedition::Hostiles;i++)if(!life.defeated[i]){
            const auto& mob=life.enemies[i];assert(!area.overlaps(mob.feet,.5));
        }
        if(level!=0)continue;
        // Every natural wood/leaf component is connected to solid soil.
        std::unordered_set<Cell,CellHash> seen;
        const auto isTree=[&](Cell p){return micro::within(p) && !(p.x>=25 && p.x<=45 && p.z>=10 && p.z<=24) && arbor::treeMaterial(area.raw(p));};
        for(int z=1;z<micro::Depth-1;z++)for(int y=1;y<micro::Height;y++)for(int x=1;x<micro::Width-1;x++){
            Cell p{x,y,z};if(!isTree(p)||seen.contains(p))continue;
            std::vector<Cell> queue{p};seen.insert(p);bool supported=false;
            for(std::size_t i=0;i<queue.size();i++){
                const auto q=queue[i];const auto down=q+Cell{0,-1,0};
                if(!isTree(down) && area.raw(down)!=Air && area.raw(q)==Timber){
                    const auto* cap=area.blocks().roof(q);
                    supported|=!cap || (q.y<=cap->low()+1e-6);
                }
                for(auto d:Sides){const auto next=q+d;if(isTree(next)&&seen.insert(next).second)queue.push_back(next);}
            }
            assert(supported);++trees;
        }
        for(int n=0;n<micro::TileCount;n++){
            micro::TerrainMeshes mesh;area.drawMeshes(mesh,micro::tileBounds(n));
            for(const auto* g:{&mesh.solid,&mesh.exact,&mesh.trees,&mesh.sprigs}){
                assert(g->points.size()%3==0);triangles+=g->points.size()/3;
                for(const auto& v:g->points)assert(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&std::isfinite(v.nx));
            }
        }
        // Diagonal tile owners must be invalidated after a corner edit.
        assert(area.set({12,9,12},area.raw({12,9,12})==Air?Stone:Air));
        for(int tile:{0,1,micro::TilesX,micro::TilesX+1})assert(area.tileVersion(tile)==area.revision);
    }
    std::cout<<rooms<<" generated rooms, "<<groundTests<<" walkable route samples, "<<trees<<" grounded tree components, "<<triangles<<" mesh triangles validated.\n";
}
