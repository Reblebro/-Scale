#pragma once
#include "render.hpp"
#include "world.hpp"
#include "tech-suit.hpp"

// First-person fictional pulse-rifle art. Coordinates are renderer-local
// artistic proportions only; no physical weapon internals are represented.
namespace rifleart {
constexpr Color graphite{.19f,.20f,.23f}, dark{.076f,.080f,.093f},
    armor{.24f,.25f,.29f}, edge{.32f,.32f,.34f},
    brass{.53f,.34f,.17f}, amber{1.0f,.30f,.045f},
    rubber{.12f,.108f,.10f}, sleeve{.20f,.15f,.12f};
inline void facingQuad(Geometry& g,Vec a,Vec b,Vec c,Vec d,Vec normal,int m,Color color){
    if(dot(cross(b-a,c-a),normal)<0)g.quad(a,d,c,b,m,color,3);
    else g.quad(a,b,c,d,m,color,3);
}
inline std::array<Vec,8> contour(Vec lo,Vec hi,double bevel,double z){
    return {{{lo.x+bevel,lo.y,z},{hi.x-bevel,lo.y,z},{hi.x,lo.y+bevel,z},
        {hi.x,hi.y-bevel,z},{hi.x-bevel,hi.y,z},{lo.x+bevel,hi.y,z},
        {lo.x,hi.y-bevel,z},{lo.x,lo.y+bevel,z}}};
}
inline void panel(Geometry& g,Vec lo,Vec hi,double bevel,Color color,int m=RifleMetalTexture){
    bevel=std::min({bevel,(hi.x-lo.x)*.24,(hi.y-lo.y)*.24,(hi.z-lo.z)*.24});
    const double inset=bevel*.65;
    const Vec a=lo+Vec{inset,inset,0},b=hi-Vec{inset,inset,0};
    const auto first=contour(a,b,bevel*.55,lo.z),second=contour(lo,hi,bevel,lo.z+bevel),
        third=contour(lo,hi,bevel,hi.z-bevel),last=contour(a,b,bevel*.55,hi.z);
    const auto center=(lo+hi)*.5;
    for(int i=0;i<8;++i){const int j=(i+1)%8;
        const Vec n=unit(Vec{second[i].x+second[j].x-2*center.x,second[i].y+second[j].y-2*center.y,0});
        facingQuad(g,first[i],first[j],second[j],second[i],n+Vec{0,0,-1},m,color);
        facingQuad(g,second[i],second[j],third[j],third[i],n,m,color);
        facingQuad(g,third[i],third[j],last[j],last[i],n+Vec{0,0,1},m,color);
        g.triangle({center.x,center.y,lo.z},first[j],first[i],m,color,3);
        g.triangle({center.x,center.y,hi.z},last[i],last[j],m,color,3);
    }
}
inline void sidePlate(Geometry& g,int side,double x,double y,double z,double height,double width,Color color,int m=RifleMetalTexture){
    // Side armor is an extruded chamfered polygon with a sloping edge band.
    // Its bevel is independent of the thin depth, unlike a scaled cube.
    const double bevel=std::min({.021,height*.20,width*.20});
    const double rim=bevel*.46;
    const auto ring=[&](double depth,double inset){
        const auto q=contour({-width*.5+inset,-height*.5+inset,0},
            {width*.5-inset,height*.5-inset,0},bevel*.70,0);
        std::array<Vec,8> r{};
        for(int i=0;i<8;++i)r[i]={x+side*depth,y+q[i].y,z+q[i].x};
        return r;
    };
    const auto back=ring(0,0),outer=ring(.008,0),front=ring(.018,rim);
    const Vec normal{double(side),0,0},center{x+side*.018,y,z};
    const Color beveled=m==RifleGlowTexture?color:Color{color.r*1.35f,color.g*1.35f,color.b*1.35f};
    for(int i=0;i<8;++i){const int j=(i+1)%8;
        const Vec radial=unit(Vec{0,outer[i].y+outer[j].y-2*y,outer[i].z+outer[j].z-2*z});
        facingQuad(g,back[i],back[j],outer[j],outer[i],radial,m,color);
        facingQuad(g,outer[i],outer[j],front[j],front[i],radial+normal,m,beveled);
        if(dot(cross(front[i]-center,front[j]-center),normal)<0)g.triangle(center,front[j],front[i],m,color,3);
        else g.triangle(center,front[i],front[j],m,color,3);
    }
}
inline void line(Geometry& g,Vec a,Vec b,double radius,Color color,int m=RifleGlowTexture){
    g.tube(a,b,radius,m,color,6);
}
inline constexpr double Scale=.64,Forward=.72,Side=.49,Drop=.32,AimDrop=Scale*.316,HipTurn=.13;
inline void build(Geometry& body,Geometry& optic,Geometry& glow,Geometry& lens){
    // Chunky dark receiver, silver-gray bevels and a stepped forward shroud.
    panel(body,{-.125,-.15,-.63},{.125,.105,.38},.035,graphite);
    panel(body,{-.135,-.17,-.47},{.135,-.075,.42},.025,dark);
    panel(body,{-.143,.075,-.60},{.143,.112,-.37},.024,armor);
    panel(body,{-.143,.075,-.34},{.143,.112,.32},.024,armor);
    panel(body,{-.115,-.10,.34},{.115,.10,.88},.028,armor);
    panel(body,{-.105,-.115,-.94},{.105,.080,-.59},.035,armor);
    panel(body,{-.132,-.17,-.96},{.132,.10,-.88},.025,dark);
    panel(body,{-.086,-.37,-.38},{.086,-.11,-.17},.024,rubber,RifleGripTexture);
    panel(body,{-.100,-.40,-.19},{.100,-.12,.11},.026,graphite);
    panel(body,{-.107,-.41,-.20},{.107,-.36,.12},.018,dark);
    for(int side:{-1,1}){
        const double s=side;
        // Raised layered plates, seam gaps, recessed decorative cells.
        sidePlate(body,side,s*.124,-.015,-.42,.22,.32,armor);
        sidePlate(body,side,s*.124,-.015,-.055,.23,.37,graphite);
        sidePlate(body,side,s*.124,-.018,.255,.20,.19,armor);
        sidePlate(body,side,s*.135,-.095,.055,.061,.16,dark);
        sidePlate(body,side,s*.135,-.10,-.52,.048,.10,dark);
        sidePlate(body,side,s*.135,.065,-.20,.034,.075,dark);
        sidePlate(body,side,s*.106,-.008,.60,.16,.48,graphite);
        sidePlate(body,side,s*.104,-.025,-.75,.15,.24,graphite);
        for(int i=0;i<5;++i){
            sidePlate(body,side,s*.124,.050,.405+i*.085,.064,.061,armor);
            sidePlate(body,side,s*.142,.050,.405+i*.085,.047,.043,dark);
            sidePlate(body,side,s*.124,-.055,.405+i*.085,.055,.061,armor);
            sidePlate(body,side,s*.142,-.055,.405+i*.085,.036,.043,dark);
        }
        // Light channels are recessed into dark backing strips, not floating.
        for(const auto& channel:std::array<Vec,4>{{{.142,.010,-.10},{.142,-.135,-.16},{.124,.004,.60},{.126,.015,-.75}}}){
            const double width=channel.z>.3?.30:channel.z<-.5?.15:.20;
            sidePlate(body,side,s*channel.x,channel.y,channel.z,.036,width+.028,dark);
            sidePlate(glow,side,s*(channel.x+.0185),channel.y,channel.z,.012,width,amber,RifleGlowTexture);
        }
        // The artwork's angular gold triangular insignia on the aft plate.
        const double x=s*.147;
        Vec a{x,.043,-.49},b{x,.043,-.37},c{x,-.066,-.46};
        line(body,a,b,.006,brass,RifleMetalTexture);line(body,b,c,.006,brass,RifleMetalTexture);line(body,c,a,.006,brass,RifleMetalTexture);
        // Small dark inset details and surface fastener-like decorative studs.
        for(double z:{-.58,-.30,-.24,.18,.32,.81,-.84})for(double y:{-.084,.067}){
            const double sideX=s*(z>.38?.130:z<-.65?.129:.145);
            body.tube({sideX,y,z},{sideX+s*.006,y,z},.012,RifleMetalTexture,dark,12);
            line(body,{sideX+s*.007,y-.005,z},{sideX+s*.007,y+.005,z},.0015,edge,RifleMetalTexture);
        }
        for(int i=0;i<3;++i)sidePlate(body,side,s*.101,-.24-i*.045,-.025,.022,.24,dark);
    }
    // Small recessed trim grooves and separate cosmetic gold marks.
    for(int side:{-1,1}){
        const double x=side*.148;
        for(double z:{-.32,.14}){
            line(body,{x,.065,z},{x,-.047,z},.0024,dark,RifleMetalTexture);
            line(body,{x,-.052,z},{x,-.065,z+.025},.0024,dark,RifleMetalTexture);
        }
        for(int i=0;i<3;++i){const double z=-.28+i*.018;
            line(body,{x,-.021,z},{x,-.007,z+.009},.0015,brass,RifleMetalTexture);
        }
    }
    // Long top rail and stepped silhouettes.
    panel(body,{-.076,.102,-.64},{.076,.151,.86},.015,dark);
    for(int i=0;i<23;++i){const double z=-.60+i*.062;
        panel(body,{-.080,.143,z},{.080,.169,z+.034},.009,graphite);
    }
    // Metallic front silhouette with amber collar, closed fictional emitter.
    body.tube({0,0,.82},{0,0,1.16},.081,RifleMetalTexture,graphite,32);
    for(double z:{.89,1.035,1.13})body.tube({0,0,z},{0,0,z+.035},.103,RifleMetalTexture,armor,32);
    glow.tube({0,0,1.073},{0,0,1.087},.099,RifleGlowTexture,amber,32);
    body.tube({0,0,1.162},{0,0,1.168},.066,RifleMetalTexture,dark,32);
    panel(body,{-.035,.100,.79},{.035,.205,.85},.009,graphite);
    // Permanent raised sight: actual open window rather than a solid cube.
    panel(optic,{-.105,.151,-.30},{.105,.198,.12},.018,dark);
    panel(optic,{-.106,.19,-.24},{.106,.222,.06},.012,armor);
    constexpr double outerX=.105,innerX=.078,centerY=.316;
    const auto frame=[&](double z,bool inner){
        const double w=inner?innerX:outerX,h=inner?.072:.106,k=inner?.018:.027;
        auto ring=contour({-w,centerY-h,0},{w,centerY+h,0},k,z);
        for(auto& p:ring)p.x*=1.-.20*std::clamp((p.y-centerY+h)/(2*h),0.0,1.0);
        return ring;
    };
    const auto outerBack=frame(-.22,false),outerFront=frame(-.105,false),
        innerBack=frame(-.221,true),innerFront=frame(-.104,true);
    for(int i=0;i<8;++i){const int j=(i+1)%8;
        const Vec n=unit(Vec{outerBack[i].x+outerBack[j].x,outerBack[i].y+outerBack[j].y-2*centerY,0});
        facingQuad(optic,outerBack[i],outerBack[j],outerFront[j],outerFront[i],n,RifleMetalTexture,armor);
        facingQuad(optic,innerBack[i],innerFront[i],innerFront[j],innerBack[j],n*(-1),RifleMetalTexture,dark);
        facingQuad(optic,outerBack[i],innerBack[i],innerBack[j],outerBack[j],{0,0,-1},RifleMetalTexture,graphite);
        facingQuad(optic,outerFront[i],outerFront[j],innerFront[j],innerFront[i],{0,0,1},RifleMetalTexture,graphite);
        line(glow,innerBack[i]+Vec{0,0,.006},innerBack[j]+Vec{0,0,.006},.0018,{.86f,.24f,.035f});
    }
    // Transparent amber coating, rendered separately without depth writes.
    const auto glass=frame(-.214,true);
    for(int i=0;i<8;++i)lens.triangle({0,centerY,-.214},glass[i],glass[(i+1)%8],RifleLensTexture,{.88f,.36f,.08f});
    // Thin stylized illuminated reticle with an open central gap.
    for(int s:{-1,1}){
        line(glow,{s*.008,centerY,-.223},{s*.027,centerY,-.223},.0024,amber);
        line(glow,{0,centerY+s*.008,-.223},{0,centerY+s*.027,-.223},.0024,amber);
        sidePlate(optic,s,s*.105,.242,-.09,.04,.10,graphite);
        glow.tube({s*.124,.242,-.105},{s*.127,.242,-.105},.006,RifleGlowTexture,amber,10);
    }
    glow.stone({0,centerY,-.224},.0026,RifleGlowTexture,amber,4,8);
    techsuit::build(body);
}
}
