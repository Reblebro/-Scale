#pragma once
#include "terrain-shape.hpp"
#include "surface-fracture.hpp"
#include <tuple>

namespace terrain {

// A root can be cut in 65,536 divisions along each axis. Retain just its
// exposed squares, coalescing equal rows and then equal spans across rows.
// Level remains part of the merge key so adjoining texture scales stay intact.
class LeafFaces {
    struct Surface {
        int side,level,plane,material;
        bool recess;
        auto operator<=>(const Surface&) const=default;
    };
    struct Run {Surface surface;int row,left,right;};
    std::vector<Run> runs;
public:
    void add(Leaf face,int side,bool recess){
        const int axis=side/2,u=(axis+1)%3,v=(axis+2)%3;
        const auto p=face.position;
        runs.push_back({{side,face.level,p[axis]+side%2,face.material,recess},p[v],p[u],p[u]+1});
    }

    void draw(Geometry& out,Vec origin,Color tint){
        std::sort(runs.begin(),runs.end(),[](const Run& a,const Run& b){
            return std::tie(a.surface,a.row,a.left)<std::tie(b.surface,b.row,b.left);
        });
        std::size_t count=0;
        for(const auto& run:runs){
            if(count && runs[count-1].surface==run.surface && runs[count-1].row==run.row &&
               runs[count-1].right==run.left)
                runs[count-1].right=run.right;
            else runs[count++]=run;
        }
        runs.resize(count);
        std::sort(runs.begin(),runs.end(),[](const Run& a,const Run& b){
            return std::tie(a.surface,a.left,a.right,a.row)<std::tie(b.surface,b.left,b.right,b.row);
        });
        for(std::size_t i=0;i<count;){
            const auto& start=runs[i++];
            int bottom=start.row+1;
            while(i<count && runs[i].surface==start.surface && runs[i].left==start.left &&
                  runs[i].right==start.right && runs[i].row==bottom){++i;++bottom;}
            const auto [side,level,plane,material,recess]=start.surface;
            const int axis=side/2,u=(axis+1)%3,v=(axis+2)%3,scale=subdivisions(level);
            Vec p;
            p[axis]=plane;p[u]=start.left;p[v]=start.row;
            const Vec a=p/scale-origin;
            Vec b=a,c=a,d=a;
            b[u]=c[u]=double(start.right)/scale-origin[u];
            c[v]=d[v]=double(bottom)/scale-origin[v];
            const float shade=recess?0.74f:1.0f;
            const Color color{tint.r*shade,tint.g*shade,tint.b*shade};
            if(side%2)chippedQuad(out,a,b,c,d,side,material,color,float(scale),origin);
            else chippedQuad(out,a,d,c,b,side,material,color,float(scale),origin);
        }
    }
};

// Keep the exact part of an interface inside the owner and above the
// neighboring slope. Both roofs share the x=z triangle diagonal.
inline void exposedRoofBoundary(Geometry& out,Leaf face,int side,const TerrainRoof* owner,
                                Leaf next,const TerrainRoof& neighbor,Vec origin,Color color){
    const auto bounds=face.bounds();
    const int axis=side/2,u=(axis+1)%3,v=(axis+2)%3;
    Vec a=bounds.lo;
    if(side%2)a[axis]=bounds.hi[axis];
    Vec b=a,c=a,d=a;
    b[u]=c[u]=bounds.hi[u];c[v]=d[v]=bounds.hi[v];
    const auto corners=side%2?std::array{a,b,c,d}:std::array{a,d,c,b};
    const auto root=parent(face.position,subdivisions(face.level));
    const auto adj=parent(next.position,subdivisions(next.level));
    const auto emit=[&](int ownerSide,int neighborSide){
        std::array<Vec,12> polygon{};
        std::copy(corners.begin(),corners.end(),polygon.begin());
        int count=4;
        const auto clip=[&](auto field){
            if(!count)return;
            std::array<Vec,12> output{};
            int size=0;
            Vec previous=polygon[count-1];
            double from=field(previous);
            for(int i=0;i<count;++i){
                const Vec p=polygon[i];const double to=field(p);
                if((from<0)!=(to<0))output[size++]=previous+(p-previous)*(from/(from-to));
                if(to>=0)output[size++]=p;
                previous=p;from=to;
            }
            polygon=output;count=size;
        };
        if(ownerSide)clip([&](Vec p){return ownerSide*owner->diagonal(p-root.v());});
        if(neighborSide)clip([&](Vec p){return neighborSide*neighbor.diagonal(p-adj.v());});
        if(owner){
            const auto below=[&](Vec p){return owner->height(p.x-root.x,p.z-root.z)-p.y;};
            if(side!=3 && std::none_of(polygon.begin(),polygon.begin()+count,[&](Vec p){return below(p)>1e-12;}))return;
            clip(below);
        }
        auto above=[&](Vec p){return p.y-neighbor.height(p.x-adj.x,p.z-adj.z);};
        // A whole coplanar contact has no exposed area.
        if(std::none_of(polygon.begin(),polygon.begin()+count,[&](Vec p){return above(p)>0;}))return;
        clip(above);
        for(int i=2;i<count;i++)out.triangle(polygon[0]-origin,polygon[i-1]-origin,polygon[i]-origin,
            face.material,color,float(subdivisions(face.level)));
    };
    if(axis!=1){emit(0,0);return;}
    for(int half:{-1,1}){
        if(owner){emit(-1,half);emit(1,half);}
        else emit(0,half);
    }
}

template<class FlatFace>
void boundary(Geometry& out,const World& world,Leaf leaf,int side,Vec origin,Color color,const FlatFace& flat){
    const auto* profile=world.leafRoof(leaf);
    world.visitFaceNeighbors(leaf,side,[&](Leaf face,Leaf next){
        if(next.material==Air || (next.material==Water && leaf.material!=Water)){
            if(profile)roofFragment(out,*profile,face,origin,color,side);
            else flat(face);
        }else if(const auto* neighbor=world.leafRoof(next))
            exposedRoofBoundary(out,face,side,profile,next,*neighbor,origin,color);
    });
}

inline void editedRoot(Geometry& out,const World& world,Cell root,Vec origin,Color tint,bool legacyShapes){
    const Vec corner=root.v();
    const bool mainlySolid=world.sample(0,root).amount>FullVolume/2;
    LeafFaces faces;
    world.visitMatter({corner,corner+Vec{1,1,1}},[&](Leaf leaf){
        const auto* profile=world.leafRoof(leaf);
        if(profile)roofFragment(out,*profile,leaf,origin,tint);
        else{
            const auto shape=world.shapedPiece(leaf);
            const auto air=legacyShapes && !shape && leaf.material!=Dirt && leaf.material!=Grass && leaf.material!=Sand?piece(world,leaf):0u;
            if(shape || air){
                const double size=1.0/subdivisions(leaf.level);
                if(shape)stone(out,leaf.position.v()*size-origin,size,leaf.material,tint,*shape);
                else stone(out,leaf.position.v()*size-origin,size,leaf.material,tint,CellHash{}(leaf.position),onFloor(air));
                return true;
            }
        }
        for(int side=0;side<6;side++){
            const int axis=side/2;
            const auto bounds=leaf.bounds();
            const bool recess=mainlySolid && (side%2?bounds.hi[axis]<corner[axis]+1:bounds.lo[axis]>corner[axis]);
            const float shade=recess?0.74f:1.0f;
            const Color color{tint.r*shade,tint.g*shade,tint.b*shade};
            boundary(out,world,leaf,side,origin,color,[&](Leaf face){faces.add(face,side,recess);});
        }
        return true;
    });
    faces.draw(out,origin,tint);
}

inline void immediateBoundary(Geometry& out,const World& world,Leaf leaf,int side,Vec origin,Color color){
    boundary(out,world,leaf,side,origin,color,[&](Leaf face){
        const double size=1.0/subdivisions(face.level);
        chippedSquare(out,face.position.v()*size-origin,size,side,face.material,color,origin);
    });
}
}
