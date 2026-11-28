#pragma once
#include "render.hpp"
#include <initializer_list>

namespace woodland {
constexpr Color coat{.52f,.34f,.21f}, cream{.82f,.74f,.60f}, umber{.23f,.13f,.075f}, hoof{.12f,.10f,.085f};
struct Ring {Vec c; double width,depth;};
inline Vec cubic(Vec a,Vec b,Vec c,Vec d,double t){return (b*2+(c-a)*t+(a*2-b*5+c*4-d)*t*t+(b*3-a-c*3+d)*t*t*t)*.5;}
// Smooth anatomical cross sections along curved centerlines; shared normals
// preserve the skin silhouette without joints made from visible cylinders.
inline void loft(Geometry& g,std::initializer_list<Ring> list,Color tint,int sides=24,int steps=4,int material=DeerFurTexture,bool fur=true){
    const std::vector<Ring> rings(list);const int end=int(rings.size())-1;
    auto sample=[&](double t,double a){
        t=std::clamp(t,0.,double(end));int i=std::min(int(t),end-1);double u=t-i;
        auto c=[&](int j){return rings[std::clamp(j,0,end)].c;};
        Vec center=cubic(c(i-1),c(i),c(i+1),c(i+2),u);
        Vec tangent=unit(cubic(c(i-1),c(i),c(i+1),c(i+2),u+.001)-cubic(c(i-1),c(i),c(i+1),c(i+2),u-.001));
        Vec x=unit(Vec{1,0,0}-tangent*tangent.x);if(length(x)<.1)x={0,0,1};
        Vec y=unit(cross(tangent,x));
        const double w=std::lerp(rings[i].width,rings[i+1].width,u),h=std::lerp(rings[i].depth,rings[i+1].depth,u);
        return center+x*(w*std::cos(a))+y*(h*std::sin(a));
    };
    auto vertex=[&](int j,int k){double t=double(j)/steps,a=2*Pi*k/sides;Vec p=sample(t,a);
        Vec n=unit(cross(sample(t,a+.001)-sample(t,a-.001),sample(t+.001,a)-sample(t-.001,a)));
        if(length(n)<.5)n=unit(Vec{std::cos(a),std::sin(a),0});
        // Directional coat, cream underside, fine deterministic pigment variation.
        double shade=fur?.91+.08*std::sin(p.x*32+p.z*21)*std::sin(p.y*29-p.z*12):1.;
        double pale=fur?std::clamp((-n.y-.12)*.60,0.,.55):0.;
        return Vertex{float(p.x),float(p.y),float(p.z),float(n.x),float(n.y),float(n.z),float(a/2/Pi*2),float(t*.45),float(material),float(std::lerp(tint.r,cream.r,pale)*shade),float(std::lerp(tint.g,cream.g,pale)*shade),float(std::lerp(tint.b,cream.b,pale)*shade)};};
    auto tri=[&](Vertex a,Vertex b,Vertex c){Vec ab{b.x-a.x,b.y-a.y,b.z-a.z},ac{c.x-a.x,c.y-a.y,c.z-a.z};Vec n=cross(ab,ac);if(length(n)<1e-10)return;if(dot(n,{a.nx+b.nx+c.nx,a.ny+b.ny+c.ny,a.nz+b.nz+c.nz})<0)std::swap(b,c);for(auto v:{a,b,c})g.points.push_back(v);};
    std::vector<Vertex> grid;grid.reserve((end*steps+1)*(sides+1));
    for(int j=0;j<=end*steps;j++)for(int k=0;k<=sides;k++)grid.push_back(vertex(j,k));
    auto at=[&](int j,int k){return grid[j*(sides+1)+k];};
    for(int j=0;j<end*steps;j++)for(int k=0;k<sides;k++){auto a=at(j,k),b=at(j,k+1),c=at(j+1,k+1),d=at(j+1,k);tri(a,b,c);tri(a,c,d);}
    for(int j:{0,end*steps}){auto center=rings[j==0?0:end].c;for(int k=0;k<sides;k++){auto a=at(j,k),b=at(j,k+1);if(j==0)std::swap(a,b);g.triangle(center,{a.x,a.y,a.z},{b.x,b.y,b.z},material,tint);}}
}
inline void deer(Geometry& g,double phase,double collapse=0){
    const bool idle=phase<0;const double bob=idle?0:.025*std::cos(phase*2);
    loft(g,{{{0,1.00+bob,-.61},.025,.035},{{0,1.00+bob,-.50},.18,.20},{{0,1.02+bob,-.31},.236,.25},{{0,1.02+bob,-.05},.205,.255},{{0,1.03+bob,.22},.218,.27},{{0,1.08+bob,.40},.167,.22},{{0,1.16+bob,.49},.05,.08}},coat,36,6);
    loft(g,{{{0,1.06+bob,.30},.155,.18},{{0,1.22+bob,.42},.14,.16},{{0,1.39+bob,.51},.102,.121},{{0,1.51+bob,.57},.078,.09}},coat,32,6);
    loft(g,{{{0,1.51+bob,.46},.025,.035},{{0,1.53+bob,.54},.108,.116},{{0,1.51+bob,.64},.105,.112},{{0,1.46+bob,.75},.077,.073},{{0,1.43+bob,.84},.053,.045},{{0,1.43+bob,.876},.023,.025}},coat,36,5);
    loft(g,{{{0,1.433+bob,.835},.045,.028},{{0,1.434+bob,.868},.049,.034},{{0,1.43+bob,.895},.028,.022}},hoof,20,4,LeatherTexture,false);
    loft(g,{{{0,1.00+bob,-.52},.074,.07},{{0,.94+bob,-.65},.09,.057},{{0,.87+bob,-.72},.012,.015}},cream,20,4);
    loft(g,{{{0,1.41+bob,.72},.060,.034},{{0,1.397+bob,.79},.057,.033},{{0,1.414+bob,.86},.035,.014}},cream,24,4);
    for(int s:{-1,1}){
        g.stone({s*.034,1.443+bob,.878},.012,LeatherTexture,{.055f,.044f,.036f},7,10);
        g.tube({s*.046,1.410+bob,.852},{s*.062,1.418+bob,.778},.004,LeatherTexture,umber,7);
        const Vec eye{s*.093,1.55+bob,.653};
        g.stone(eye,.029,LeatherTexture,{.095f,.060f,.032f},12,18);
        g.stone(eye+Vec{s*.011,.009,.018},.006,LeatherTexture,{.67f,.53f,.34f},6,10);
        // Broad, cupped ears with a separate inner surface and curled tip.
        loft(g,{{{s*.068,1.59+bob,.51},.045,.025},{{s*.15,1.68+bob,.48},.065,.022},{{s*.24,1.74+bob,.43},.043,.014},{{s*.28,1.77+bob,.40},.004,.004}},coat,24,5);
        loft(g,{{{s*.103,1.622+bob,.533},.021,.008},{{s*.17,1.69+bob,.505},.041,.008},{{s*.251,1.743+bob,.437},.004,.004}},Color{.66f,.43f,.34f},18,4,SkinTexture,false);
        // Three limb segments, with staggered lifting and a flexed hock.
        for(int f:{-1,1}){
            double a=phase+(s*f<0?Pi:0)+(f<0?.20:0),swing=idle?0:std::sin(a),lift=idle?0:.13*std::max(0.,std::cos(a));
            double z=f>0?.32:-.36;Vec hip{s*.145,.94+bob,z},knee{s*.15,.62+bob,z+(f<0?.135:-.025)+swing*.09},hock{s*.15,.32+lift*.55,z+(f<0?-.10:.02)+swing*.15},ankle{s*.15,.087+lift,z+swing*.23};
            knee.y-=.10*collapse;knee.z+=.16*collapse;
            hock.y+=.10*collapse;hock.z-=.22*collapse;
            ankle.y+=.30*collapse;ankle.z+=.22*collapse;
            loft(g,{{hip,.095,.126},{hip+(knee-hip)*.38,.082,.095},{knee,.048,.057},{hock,.033,.039},{ankle,.026,.031}},coat,20,5);
            for(int toe:{-1,1}){Vec foot=ankle+Vec{toe*.023,-.077,.028};loft(g,{{foot,.018,.034},{foot+Vec{0,.025,0},.022,.043},{foot+Vec{0,.069,-.023},.015,.023}},hoof,12,3,LeatherTexture,false);}
        }
    }
}
}
