#pragma once
#include "render.hpp"
#include "world.hpp"
#include <vector>

namespace arbor {
inline bool treeMaterial(int id){return id==Timber || id==Leaves;}
// A dual mesh on the lattice of voxel centers. Only sign-changing edges emit
// a face; the four incident cubes share the same averaged crossing vertex.
// One owner (the solid sample) emits each face, even across chunk boundaries.
// This keeps the source matter editable without rendering every cube face.
template<class SampleTree>
void mesh(Geometry& shell,Geometry& sprigs,Bounds bounds,SampleTree sample,bool detail){
    const Cell lo=bounds.lo-Cell{1,1,1};
    const Cell dim=bounds.hi-bounds.lo+Cell{2,2,2};
    const auto index=[&](Cell p){p=p-lo;return (std::size_t(p.z)*dim.y+p.y)*dim.x+p.x;};
    const auto count=std::size_t(dim.x)*dim.y*dim.z;
    std::vector<unsigned char> matter(count);
    for(int z=lo.z;z<lo.z+dim.z;++z)for(int y=lo.y;y<lo.y+dim.y;++y)
        for(int x=lo.x;x<lo.x+dim.x;++x){const Cell p{x,y,z};matter[index(p)]=sample(p);}
    const auto id=[&](Cell p){return matter[index(p)];};
    struct Dual {Vec p,n;bool ready=false;};
    std::vector<Dual> dual(count);
    const auto vertex=[&](Cell c)->Dual{
        auto& v=dual[index(c)];if(v.ready)return v;
        Vec sum{},normal{};int crossings=0;
        for(int axis=0;axis<3;++axis){
            const int u=(axis+1)%3,w=(axis+2)%3;
            for(int a=0;a<2;++a)for(int b=0;b<2;++b){
                Cell from=c;from[u]+=a;from[w]+=b;Cell to=from;to[axis]++;
                const bool solid=id(from)!=Air,other=id(to)!=Air;
                if(solid==other)continue;
                Vec p=(from-bounds.lo).v()+Vec{.5,.5,.5};p[axis]+=.5;
                sum+=p;normal[axis]+=solid?1.0:-1.0;++crossings;
            }
        }
        if(crossings){v.p=sum/double(crossings);v.n=unit(normal);}
        v.ready=true;return v;
    };
    const auto triangle=[&](Dual a,Dual b,Dual c,int material,Vec face){
        const Vec geometric=unit(cross(b.p-a.p,c.p-a.p));
        if(length(geometric)<1e-8)return;
        if(dot(geometric,face)<0)std::swap(b,c);
        for(const auto& v:{a,b,c}){
            const Vec n=unit(v.n+face*.24);
            const Vec p=v.p;
            shell.points.push_back({float(p.x),float(p.y),float(p.z),float(n.x),float(n.y),float(n.z),
                float(p.x+p.z),float(p.y),float(material),1,1,1});
        }
    };
    for(int z=bounds.lo.z;z<bounds.hi.z;++z)for(int y=bounds.lo.y;y<bounds.hi.y;++y)
        for(int x=bounds.lo.x;x<bounds.hi.x;++x){
            const Cell p{x,y,z};const int material=id(p);if(!material)continue;
            for(int side=0;side<6;++side){
                if(id(p+Sides[side]))continue;
                const int axis=side/2,u=(axis+1)%3,w=(axis+2)%3;
                Cell c=p;if(!(side%2))c[axis]--;c[u]--;c[w]--;
                Cell d=c,e=c,f=c;d[u]++;e[u]++;e[w]++;f[w]++;
                const auto a=vertex(c),b=vertex(d),q=vertex(e),r=vertex(f);
                const Vec n=Sides[side].v();
                triangle(a,b,q,material,n);triangle(a,q,r,material,n);
                // Small folded, double-sided leaves on a subset of exposed
                // crown faces. Actual silhouette detail, not a green texture.
                if(!detail || material!=Leaves)continue;
                const auto key=CellHash{}(p+Sides[side]*97);
                if(key%2!=0)continue;
                const Vec center=(a.p+b.p+q.p+r.p)/4.0;
                Vec tangent{};tangent[u]=1;Vec bitangent{};bitangent[w]=1;
                for(int i=0;i<4;++i){
                    const auto k=mix(std::uint32_t(key)+i*0x9e3779b9u);
                    const double angle=(k&255)*2*Pi/256.0;
                    const Vec direction=tangent*std::cos(angle)+bitangent*std::sin(angle);
                    const Vec across=tangent*(-std::sin(angle))+bitangent*std::cos(angle);
                    const Vec root=center+direction*(.04+.09*i)-n*.025;
                    const double length=.36+double((k>>8)&255)/255.0*.38;
                    const Vec tip=root+direction*length+n*(length*.45);
                    const Vec mid=(root+tip)*.5+n*.07;
                    const Vec left=(root+tip)*.5+across*(length*.25);
                    const Vec right=(root+tip)*.5-across*(length*.25);
                    const float variant=float((k>>16)&255)/255.0f;
                    const Color color{.84f+.34f*variant,.93f+.20f*variant,.78f+.22f*variant};
                    for(const auto& t:std::array<std::array<Vec,3>,4>{{{root,left,mid},{left,tip,mid},{tip,right,mid},{right,root,mid}}}){
                        sprigs.triangle(t[0],t[1],t[2],Leaves,color);
                        // The leaf draw is two-sided with a flipped back-face
                        // normal in the shader. Do not upload a duplicate face.
                    }
                }
            }
        }
}
}
