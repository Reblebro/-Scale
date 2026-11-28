#pragma once
#include "render.hpp"
#include "world.hpp"

namespace terrain {
inline void roofTop(Geometry& mesh,const TerrainRoof& roof,Vec corner,int id,Color color){
    const auto vertices=roof.vertices();
    for(const auto& f:TerrainRoof::faces)
        mesh.triangle(corner+vertices[f[0]],corner+vertices[f[1]],corner+vertices[f[2]],id,color,0.73f);
}

inline void roofWall(Geometry& mesh,const TerrainRoof& roof,const TerrainRoof* neighbor,
    Cell root,Vec origin,int side,int id,Color color){
    const int axis=side/2,u=2-axis;
    const auto vertex=[&](double progress){
        Vec end{};
        end[axis]=side%2;end[u]=progress;
        return end;
    };
    const auto first=vertex(0),last=vertex(1);
    const double a=roof.height(first.x,first.z),b=roof.height(last.x,last.z);
    const auto next=[&](Vec point){
        if(!neighbor)return double(root.y);
        point[axis]=1-point[axis];
        return neighbor->height(point.x,point.z);
    };
    const double c=next(first),d=next(last);
    if(a<=c && b<=d)return;
    std::array<double,8> points{0,1};
    int count=2;
    const auto crossAt=[&](double from,double to,double level){
        if((from<level && to>level) || (from>level && to<level))points[count++]=(level-from)/(to-from);
    };
    crossAt(a,b,root.y);crossAt(a,b,root.y+1);
    crossAt(c,d,root.y);crossAt(c,d,root.y+1);
    crossAt(a-c,b-d,0);
    for(int i=1;i<count;i++)for(int j=i;j>0 && points[j-1]>points[j];--j)
        std::swap(points[j-1],points[j]);
    const auto height=[&](double from,double to,double t){return std::clamp(std::lerp(from,to,t),double(root.y),root.y+1.0);};
    const auto at=[&](double t,double h){
        Vec p=vertex(t);
        p.y=h;
        return p+Vec{double(root.x),0,double(root.z)}-origin;
    };
    for(int i=1;i<count;i++){
        const double l=points[i-1],r=points[i];
        const double low=height(c,d,l),high=height(a,b,l),lowRight=height(c,d,r),highRight=height(a,b,r);
        if(r-l<1e-8 || (high<=low && highRight<=lowRight))continue;
        const auto p0=at(l,std::min(low,high)),p1=at(r,std::min(lowRight,highRight));
        const auto q0=at(l,high),q1=at(r,highRight);
        if((side==1 || side==4))mesh.quad(p0,q0,q1,p1,id,color,0.71f);
        else mesh.quad(p0,p1,q1,q0,id,color,0.71f);
    }
}

// Clip the original terrain plane and each exposed cut face to the exact
// occupied leaf. A mined chip never flattens the rest of its slope.
inline void roofFragment(Geometry& mesh,const TerrainRoof& roof,Leaf leaf,Vec origin,Color color,int face=-1){
    const auto bounds=leaf.bounds();
    const Cell root=parent(leaf.position,subdivisions(leaf.level));
    const Vec anchor{double(root.x),0,double(root.z)};
    const Box limit{bounds.lo-anchor,bounds.hi-anchor};
    if(limit.lo.y>=roof.high())return;
    const float repeat=float(subdivisions(leaf.level));
    const auto emit=[&](std::array<Vec,12> polygon,int count,bool cap){
        const auto clip=[&](const auto& field){
            if(!count)return;
            std::array<Vec,12> clipped{};
            int size=0;
            Vec prior=polygon[count-1];
            double distance=field(prior);
            for(int i=0;i<count;i++){
                const Vec point=polygon[i];
                const double next=field(point);
                if((distance<0)!=(next<0))clipped[size++]=prior+(point-prior)*(distance/(distance-next));
                if(next>=0)clipped[size++]=point;
                prior=point;distance=next;
            }
            polygon=clipped;count=size;
        };
        if(cap){
            for(int axis=0;axis<3;axis++){
                clip([&](Vec p){return p[axis]-limit.lo[axis];});
                clip([&](Vec p){return limit.hi[axis]-p[axis];});
            }
        }else{
            const auto below=[&](Vec p){return roof.height(p.x,p.z)-p.y;};
            if(face!=3 && std::none_of(polygon.begin(),polygon.begin()+count,[&](Vec p){return below(p)>1e-12;}))return;
            clip(below);
        }
        const auto offset=anchor-origin;
        for(int i=2;i<count;i++)mesh.triangle(polygon[0]+offset,polygon[i-1]+offset,polygon[i]+offset,
            leaf.material,color,cap?0.73f:repeat);
    };
    if(face<0){
        if(limit.lo.y>=roof.high() || limit.hi.y<=roof.low())return;
        const auto points=roof.vertices();
        for(const auto& triangle:TerrainRoof::faces)
            emit({points[triangle[0]],points[triangle[1]],points[triangle[2]]},3,true);
        return;
    }
    const int axis=face/2,u=(axis+1)%3,v=(axis+2)%3;
    // The upper and lower faces cross the roof's diagonal. Clip both
    // halves independently, so their cut edges meet the two roof slopes.
    Vec a=limit.lo;
    if(face%2)a[axis]=limit.hi[axis];
    Vec b=a,c=a,d=a;
    b[u]=c[u]=limit.hi[u];c[v]=d[v]=limit.hi[v];
    const auto points=face%2?std::array{a,b,c,d}:std::array{a,d,c,b};
    if(axis!=1){
        emit({points[0],points[1],points[2],points[3]},4,false);
        return;
    }
    for(double sign:{-1.0,1.0}){
        std::array<Vec,12> half{};
        int count=0;
        Vec prior=points.back();
        double distance=sign*roof.diagonal(prior);
        for(Vec point:points){
            const double next=sign*roof.diagonal(point);
            if((distance<0)!=(next<0))half[count++]=prior+(point-prior)*(distance/(distance-next));
            if(next>=0)half[count++]=point;
            prior=point;distance=next;
        }
        emit(half,count,false);
    }
}

inline bool mineral(int id){
    return id==Stone || id==CopperOre || id==IronOre || id==Coal || id==Lodestone || id==Quartz;
}
inline bool organic(int id){
    return id==Grass || id==Dirt || id==Sand || id==Clay || id==Timber || id==Leaves;
}

inline bool beveled(unsigned air,int side){
    return (air&(1u<<side)) && (air&~(3u<<(side/2*2)));
}

template<class Sample>
unsigned exposed(Sample sample){
    unsigned result=0;
    for(int side=0;side<6;side++)if(!sample(Sides[side]))result|=1u<<side;
    return result;
}

// Cut only exposed corners. The midpoint is shared by the two faces, while
// endpoints stay fixed so adjacent cells can be meshed independently.
inline void face(Geometry& mesh,Vec origin,double size,int side,int material,Color color,std::uint32_t key,unsigned air=0){
    const int axis=side/2,u=(axis+1)%3,v=(axis+2)%3;
    Vec a=origin;
    if(side%2)a[axis]+=size;
    Vec b=a,c=a,d=a;
    b[u]+=size;c[u]+=size;c[v]+=size;d[v]+=size;
    Vec ridge=(a+c)*0.5;
    ridge[u]+=size*(double(key&15)-7.5)*0.017;
    ridge[v]+=size*(double((key>>4)&15)-7.5)*0.017;
    ridge[axis]+=(side%2?-1:1)*size*(0.035+double((key>>8)&15)*0.0047);
    const std::array<Vec,4> corners=side%2?std::array{a,b,c,d}:std::array{a,d,c,b};
    for(int i=0;i<4;i++){
        const Vec p=corners[i],q=corners[(i+1)%4];
        const int edge=p[u]==q[u]?u:v;
        const int adjacent=2*edge+(p[edge]>origin[edge]?1:0);
        const float repeat=float(1/size);
        if(!beveled(air,side) || !(air&(1u<<adjacent))){
            mesh.triangle(p,q,ridge,material,color,repeat);
            continue;
        }
        Vec bevel=(p+q)*0.5;
        const double inset=size*(0.105+double((key>>12)&7)*0.006);
        bevel[axis]+=(side%2?-1:1)*inset;
        bevel[edge]+=(adjacent%2?-1:1)*inset;
        mesh.triangle(p,bevel,ridge,material,color,repeat);
        mesh.triangle(bevel,q,ridge,material,color,repeat);
    }
}

inline bool bevelCell(std::uint32_t key){return (key&7)<2;}

inline bool onFloor(unsigned air){return air==(63u&~(1u<<2));}

inline unsigned piece(const World& matter,Leaf cell){
    if(!mineral(cell.material) && !organic(cell.material))return 0;
    const auto air=exposed([&](Cell side){return matter.sample(cell.level,cell.position+side).amount;});
    if(air==63)return air;
    if(mineral(cell.material) && onFloor(air) && matter.sample(cell.level,cell.position+Sides[2]).amount==volume(cell.level))return air;
    return 0;
}

inline void stone(Geometry& mesh,Vec origin,double size,int material,Color tint,const TerrainStone& profile){
    profile.triangles([&](Vec a,Vec b,Vec c){
        mesh.triangle(origin+a*size,origin+b*size,origin+c*size,material,tint,float(1/size));
    },true);
}

inline void stone(Geometry& mesh,Vec origin,double size,int material,Color tint,std::uint32_t key,bool grounded=false){
    stone(mesh,origin,size,material,tint,TerrainStone(key,material==Quartz || material==Lodestone,grounded));
}
}
