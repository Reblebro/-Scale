#pragma once
#include "world.hpp"
#include "render.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace terrain {
// This module alters the RENDERED shell only; the edit tree, saves, ray casts,
// and collision keep their original exact voxel ownership.
//
// Every surface samples the SAME world-space 3D displacement field.  Therefore
// vertices that coincide across neighboring faces are moved to identical
// positions, including at corners where three faces meet. Unlike the previous
// "chipped" treatment, the outer perimeter itself is no longer a rectangle.
inline double surfaceNoise(Vec world,int channel){
    constexpr double spacing=0.5;
    const double gx=world.x/spacing,gy=world.y/spacing,gz=world.z/spacing;
    const int ix=int(std::floor(gx)),iy=int(std::floor(gy)),iz=int(std::floor(gz));
    const double fx=gx-ix,fy=gy-iy,fz=gz-iz;
    const auto smooth=[](double t){return t*t*(3-2*t);};
    const double sx=smooth(fx),sy=smooth(fy),sz=smooth(fz);
    const auto sample=[&](int dx,int dy,int dz){
        const Cell key{ix+dx+channel*101,iy+dy-channel*79,iz+dz+channel*53};
        return double(CellHash{}(key)&65535)/32767.5-1.0;
    };
    const auto mix=[](double a,double b,double t){return a+(b-a)*t;};
    const double p00=mix(sample(0,0,0),sample(1,0,0),sx);
    const double p10=mix(sample(0,1,0),sample(1,1,0),sx);
    const double p01=mix(sample(0,0,1),sample(1,0,1),sx);
    const double p11=mix(sample(0,1,1),sample(1,1,1),sx);
    return mix(mix(p00,p10,sy),mix(p01,p11,sy),sz);
}

inline Vec warpedSurfacePoint(Vec point,Vec worldOrigin){
    const Vec global=point+worldOrigin;
    // The three components are independent but all faces use the SAME values.
    // These displacements deliberately move face edges and corners.
    return point+Vec{0.14*surfaceNoise(global,0),
                     0.16*surfaceNoise(global,1),
                     0.14*surfaceNoise(global,2)};
}

// Split on world-space grid lines rather than dividing each face into arbitrary
// halves. Adjacent same-LOD faces then agree on the sampling of their borders.
// Use coarser lines on very large greedy quads to avoid enormous vertex counts.
inline std::vector<double> surfaceCuts(Vec start,Vec edge,Vec worldOrigin){
    std::vector<double> cuts{0.0};
    const int axis=std::abs(edge.x)>1e-9?0:(std::abs(edge.y)>1e-9?1:2);
    const double delta=edge[axis];
    if(std::abs(delta)<1e-9){cuts.push_back(1.0);return cuts;}
    const double lo=start[axis]+worldOrigin[axis];
    const double hi=lo+delta;
    double step=0.5;
    while(std::abs(delta)/step>8.0)step*=2.0;
    const double begin=std::min(lo,hi),end=std::max(lo,hi);
    for(double k=std::floor(begin/step)+1;k*step<end-1e-8;++k){
        const double t=(k*step-lo)/delta;
        if(t>1e-8 && t<1.0-1e-8)cuts.push_back(t);
    }
    cuts.push_back(1.0);
    std::sort(cuts.begin(),cuts.end());
    return cuts;
}

inline void chippedQuad(Geometry& out,Vec a,Vec b,Vec c,Vec d,int side,int material,
                        Color color,float repeat,Vec worldOrigin={}){
    const Vec edgeU=b-a,edgeV=d-a;
    const double width=length(edgeU),height=length(edgeV);
    if(!sculptable(material) || material==CampTile || width<1.0/256 || height<1.0/256){
        out.quad(a,b,c,d,material,color,repeat);
        return;
    }
    const auto us=surfaceCuts(a,edgeU,worldOrigin);
    const auto vs=surfaceCuts(a,edgeV,worldOrigin);
    for(std::size_t j=1;j<vs.size();++j)for(std::size_t i=1;i<us.size();++i){
        const double u0=us[i-1],u1=us[i],v0=vs[j-1],v1=vs[j];
        const Vec p=warpedSurfacePoint(a+edgeU*u0+edgeV*v0,worldOrigin);
        const Vec q=warpedSurfacePoint(a+edgeU*u1+edgeV*v0,worldOrigin);
        const Vec r=warpedSurfacePoint(a+edgeU*u1+edgeV*v1,worldOrigin);
        const Vec t=warpedSurfacePoint(a+edgeU*u0+edgeV*v1,worldOrigin);
        Vec center=warpedSurfacePoint(a+edgeU*((u0+u1)*0.5)+edgeV*((v0+v1)*0.5),worldOrigin);
        const Vec worldCenter=center+worldOrigin;
        const double grit=surfaceNoise(worldCenter,3);
        // Small local crests and crevices stop long dirt sides from reading
        // as flat textured rectangles. This has no gameplay collision effect.
        const double relief=std::min({width*(u1-u0),height*(v1-v0),0.5})*0.075*grit;
        center[side/2]+=(side%2?1.0:-1.0)*relief;
        const double wall=1.0-std::abs(unit(cross(q-p,t-p)).y);
        const float tone=float(0.93+0.08*surfaceNoise(worldCenter,4)-0.10*wall);
        const Color tint{color.r*tone,color.g*tone*float(1-0.04*wall),
                         color.b*tone*float(1-0.03*wall)};
        out.triangle(p,q,center,material,tint,repeat);
        out.triangle(q,r,center,material,tint,repeat);
        out.triangle(r,t,center,material,tint,repeat);
        out.triangle(t,p,center,material,tint,repeat);
    }
}

inline void chippedSquare(Geometry& out,Vec start,double size,int side,int material,
                          Color color,Vec worldOrigin={}){
    const int axis=side/2,u=(axis+1)%3,v=(axis+2)%3;
    Vec a=start;
    if(side%2)a[axis]+=size;
    Vec b=a,c=a,d=a;
    b[u]+=size;c[u]+=size;c[v]+=size;d[v]+=size;
    if(side%2)chippedQuad(out,a,b,c,d,side,material,color,float(1/size),worldOrigin);
    else chippedQuad(out,a,d,c,b,side,material,color,float(1/size),worldOrigin);
}
}
