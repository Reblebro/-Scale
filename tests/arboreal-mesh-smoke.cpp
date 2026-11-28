#include "../src/arboreal-mesh.hpp"
#include "../src/natural-world.hpp"
#include <cassert>
#include <iostream>
#include <map>
#include <set>
#include <unordered_set>

// CPU-only test of the exact tree vertex stream (no OpenGL needed).
void Geometry::triangle(Vec a,Vec b,Vec c,int id,Color color,float){
    const Vec n=unit(cross(b-a,c-a));
    if(length(n)<1e-9)return;
    for(Vec p:{a,b,c})points.push_back({float(p.x),float(p.y),float(p.z),float(n.x),float(n.y),float(n.z),0,0,float(id),color.r,color.g,color.b});
}
using Triangle=std::array<std::array<int,7>,3>;
std::multiset<Triangle> triangles(const Geometry& g,Cell origin){
    std::multiset<Triangle> result;
    for(std::size_t i=0;i<g.points.size();i+=3){
        Triangle face{};
        for(int j=0;j<3;++j){const auto& v=g.points[i+j];
            face[j]={int(std::lround((v.x+origin.x)*10000)),int(std::lround((v.y+origin.y)*10000)),int(std::lround((v.z+origin.z)*10000)),
                     int(std::lround(v.nx*10000)),int(std::lround(v.ny*10000)),int(std::lround(v.nz*10000)),int(v.id)};
            assert(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z));
            assert(std::abs(length({v.nx,v.ny,v.nz})-1.0)<1e-4);
        }
        std::sort(face.begin(),face.end());result.insert(face);
    }
    return result;
}
int main(){
    // Rounded tree meshes may cross tile boundaries but must be identical
    // to the mesh generated in one larger tile, including shading and sprigs.
    const auto sample=[](Cell p){const Vec d=p.v()-Vec{7.2,8.1,6.8};
        return dot(d,d)<25?(p.y<7?Timber:Leaves):Air;};
    Geometry whole,wholeLeaves,lhs,lhsLeaves,rhs,rhsLeaves;
    arbor::mesh(whole,wholeLeaves,{{0,0,0},{16,16,16}},sample,true);
    arbor::mesh(lhs,lhsLeaves,{{0,0,0},{8,16,16}},sample,true);
    arbor::mesh(rhs,rhsLeaves,{{8,0,0},{16,16,16}},sample,true);
    const auto right=triangles(rhs,{8,0,0});auto merged=triangles(lhs,{});
    merged.insert(right.begin(),right.end());assert(merged==triangles(whole,{}));
    const auto leavesRight=triangles(rhsLeaves,{8,0,0});auto leavesMerged=triangles(lhsLeaves,{});
    leavesMerged.insert(leavesRight.begin(),leavesRight.end());assert(leavesMerged==triangles(wholeLeaves,{}));
    assert(!whole.points.empty() && !wholeLeaves.points.empty());
    // Closed manifold for a simple rounded volume, no exposed cube corners.
    using Point=std::array<int,3>;std::map<std::pair<Point,Point>,int> edges;
    bool rounded=false;
    for(std::size_t i=0;i<whole.points.size();i+=3){
        Point a[3];for(int j=0;j<3;++j){const auto& p=whole.points[i+j];a[j]={int(std::lround(p.x*10000)),int(std::lround(p.y*10000)),int(std::lround(p.z*10000))};rounded|=(a[j][0]%10000!=0 && a[j][1]%10000!=0);}
        for(int j=0;j<3;++j){auto u=a[j],v=a[(j+1)%3];if(v<u)std::swap(u,v);++edges[{u,v}];}
    }
    assert(rounded);for(const auto& [edge,count]:edges)assert(count==2);
    // Mining removes material from the new mesh; it is not a decorative tree
    // that ignores the source occupancy.
    Geometry mined,unused;arbor::mesh(mined,unused,{{0,0,0},{16,16,16}},[&](Cell p){return p.x>=7?int(Air):int(sample(p));},false);
    for(const auto& v:mined.points)assert(v.x<=7.00001f);
    // Every generated branch and leaf cell in this seed's sample must be
    // reachable from the trunk through face-adjacent matter (no floating crowns).
    int tested=0;
    for(std::uint64_t seed:{62108432ULL,1ULL,987654321ULL,0xabcdef1234567890ULL})
      for(int z=-10;z<10;++z)for(int x=-10;x<10;++x)if(auto t=natural::treeAtGrid(x,z,seed)){
        std::unordered_set<Cell,CellHash> cells;const int r=natural::TreeReach;
        const Bounds area{{t->x-r,t->base-4,t->z-r},{t->x+r+1,ForestWorldHeight-1,t->z+r+1}};
        natural::rasterTree(*t,area,[&](int x,int y,int z,int){cells.insert({x,y,z});});
        // Sample the independent point-query API too, including empty cells.
        for(int k=0;k<200;++k){
            const auto h=mix(t->key+unsigned(k));
            const Cell c{t->x+int(h%73)-36,t->base+int((h>>8)%unsigned(t->height+5))-3,t->z+int((h>>16)%73)-36};
            assert(cells.contains(c)==bool(natural::treeMatter(*t,c.x,c.y,c.z)));
        }
        std::vector<Cell> queue{{t->x,t->base,t->z}};assert(cells.erase(queue.front()));
        for(std::size_t i=0;i<queue.size();++i)for(auto d:Sides){auto p=queue[i]+d;if(cells.erase(p))queue.push_back(p);}
        assert(cells.empty());++tested;
    }
    assert(tested>200);
    std::cout<<"PASS: rounded watertight test volume, tile-identical geometry and normals, leaf sprigs, mining response, "<<tested<<" connected trees\n";
}
