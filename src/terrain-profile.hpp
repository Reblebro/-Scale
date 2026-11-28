#pragma once

#include "math.hpp"

struct TerrainRoof {
    std::array<double,4> elevation{};
    // Horizontal footprint in the owning root's coordinates. Natural terrain
    // spans a whole root, while shovel patches use the active brush lattice.
    double x0{},z0{},span=1;
    double diagonal(Vec p) const {return (p.x-x0)-(p.z-z0);}
    double low() const {return *std::min_element(elevation.begin(),elevation.end());}
    double high() const {return *std::max_element(elevation.begin(),elevation.end());}
    bool covers(int y) const {return y<high() && y+1>low();}
    double height(double x,double z) const {
        x=(x-x0)/span;z=(z-z0)/span;
        const auto [a,b,c,d]=elevation;
        return x>=z?a+(b-a)*x+(c-b)*z:a+(c-d)*x+(d-a)*z;
    }
    Vec normal(double x,double z) const {
        const auto [a,b,c,d]=elevation;
        return unit(x-x0>=z-z0?Vec{a-b,span,b-c}:Vec{d-c,span,a-d});
    }
    std::array<Vec,4> vertices() const{
        return {{{x0,elevation[0],z0},{x0+span,elevation[1],z0},{x0+span,elevation[2],z0+span},{x0,elevation[3],z0+span}}};
    }
    static constexpr std::array<std::array<int,3>,2> faces={{{0,3,2},{0,2,1}}};

    // A triangle clipped to the footprint gives the exact high point and
    // horizontal extent of the solid, including when only one corner is solid.
    bool occupied(Box& box,double minimumHeight) const {
        const double a=height(box.lo.x,box.lo.z),b=height(box.hi.x,box.lo.z);
        const double c=height(box.hi.x,box.hi.z),d=height(box.lo.x,box.hi.z);
        double lowest=std::min({a,b,c,d}),highest=std::max({a,b,c,d});
        const double shift=z0-x0;
        const double first=std::max(box.lo.x,box.lo.z-shift),last=std::min(box.hi.x,box.hi.z-shift);
        if(first<last){
            const double left=height(first,first+shift),right=height(last,last+shift);
            lowest=std::min({lowest,left,right});
            highest=std::max({highest,left,right});
        }
        if(highest<=minimumHeight)return false;
        if(lowest>=minimumHeight){
            box.hi.y=std::min(box.hi.y,highest);
            return true;
        }
        const auto points=vertices();
        Vec low{1e30,1e30,1e30},high{-1e30,-1e30,-1e30};
        for(const auto& face:faces){
            std::array<Vec,12> polygon{points[face[0]],points[face[1]],points[face[2]]};
            int count=3;
            const auto clip=[&](int axis,double edge,double sign){
                if(!count)return;
                std::array<Vec,12> output{};
                int size=0;
                auto from=polygon[count-1];
                double value=(from[axis]-edge)*sign;
                for(int i=0;i<count;i++){
                    const auto to=polygon[i];
                    const double next=(to[axis]-edge)*sign;
                    if((value<0)!=(next<0))output[size++]=from+(to-from)*(value/(value-next));
                    if(next>=0)output[size++]=to;
                    from=to;value=next;
                }
                count=size;polygon=output;
            };
            clip(0,box.lo.x,1);clip(0,box.hi.x,-1);
            clip(2,box.lo.z,1);clip(2,box.hi.z,-1);
            clip(1,minimumHeight,1);
            for(int i=0;i<count;i++)for(int axis=0;axis<3;axis++){
                low[axis]=std::min(low[axis],polygon[i][axis]);
                high[axis]=std::max(high[axis],polygon[i][axis]);
            }
        }
        if(high.y<=minimumHeight || high.x<low.x || high.z<low.z)return false;
        box.lo.x=low.x;box.hi.x=high.x;
        box.lo.z=low.z;box.hi.z=high.z;
        box.hi.y=std::min(box.hi.y,high.y);
        return true;
    }

    double intercept(Vec origin,Vec direction,double begin,double end) const {
        const auto points=vertices();
        const auto entry=origin+direction*begin;
        if(entry.y<=height(std::clamp(entry.x,0.0,1.0),std::clamp(entry.z,0.0,1.0))+1e-9)return begin;
        for(const auto& face:faces){
            const Vec a=points[face[0]],b=points[face[1]],c=points[face[2]];
            const Vec normal=cross(b-a,c-a);
            const double incidence=dot(normal,direction);
            if(incidence>=-1e-12*span*span)continue;
            const double distance=dot(normal,a-origin)/incidence;
            if(distance<begin || distance>=end)continue;
            const Vec point=origin+direction*distance;
            if(point.x<x0-1e-8 || point.x>x0+span+1e-8 || point.z<z0-1e-8 || point.z>z0+span+1e-8)continue;
            if((face[1]==3 && diagonal(point)<=1e-8) || (face[1]==2 && diagonal(point)>=-1e-8))
                end=distance;
        }
        return end;
    }
};
