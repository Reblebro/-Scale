#pragma once
#include "render.hpp"

// Authored first-person support glove. All coordinates are in the existing
// rifle view-model space. No animation or gameplay state is stored here.
namespace techsuit {
constexpr Color weave{.080f,.084f,.102f}, elastic{.039f,.042f,.052f},
    ceramic{.15f,.165f,.195f}, bevel{.22f,.235f,.27f},
    carbon{.080f,.089f,.113f}, violet{.43f,.12f,.85f};
struct Ring {Vec center;double wide,deep;};
struct Sample {Vec p,n;double u,v;};
inline void tri(Geometry& g,Sample a,Sample b,Sample c,int mat,Color color){
    const Vec n=cross(b.p-a.p,c.p-a.p);
    if(length(n)<1e-10)return;
    if(dot(n,a.n+b.n+c.n)<0)std::swap(b,c);
    for(const auto& q:{a,b,c})g.points.push_back({float(q.p.x),float(q.p.y),float(q.p.z),
        float(q.n.x),float(q.n.y),float(q.n.z),float(q.u),float(q.v),float(mat),color.r,color.g,color.b});
}
// A continuous oval sweep gives the sleeve, palm and fingers smooth normals
// and continuous UVs, unlike separate cylinders with capped joint seams.
inline void sweep(Geometry& g,const std::vector<Ring>& rings,Vec lateral,int mat,Color color,int sides=24){
    std::vector<Sample> grid;grid.reserve(rings.size()*(sides+1));
    double distance=0;
    for(size_t j=0;j<rings.size();++j){
        const auto& r=rings[j];
        if(j)distance+=length(r.center-rings[j-1].center);
        const Vec axis=unit(rings[std::min(j+1,rings.size()-1)].center-rings[j?j-1:0].center);
        const Vec x=unit(lateral-axis*dot(lateral,axis)),y=cross(axis,x);
        for(int i=0;i<=sides;++i){
            const double a=2*Pi*i/sides,cs=std::cos(a),sn=std::sin(a);
            grid.push_back({r.center+x*(r.wide*cs)+y*(r.deep*sn),unit(x*(cs/r.wide)+y*(sn/r.deep)),
                a*(r.wide+r.deep)*.5,distance});
        }
    }
    const auto at=[&](size_t j,int i){return grid[j*(sides+1)+i];};
    for(size_t j=0;j+1<rings.size();++j)for(int i=0;i<sides;++i){
        tri(g,at(j,i),at(j,i+1),at(j+1,i+1),mat,color);
        tri(g,at(j,i),at(j+1,i+1),at(j+1,i),mat,color);
    }
    for(size_t j:{size_t(0),rings.size()-1}){
        const Vec n=unit(j?rings[j].center-rings[j-1].center:rings[0].center-rings[1].center);
        const Sample center{rings[j].center,n,0,0};
        for(int i=0;i<sides;++i){auto a=at(j,i),b=at(j,i+1);a.n=b.n=n;tri(g,center,a,b,mat,color);}
    }
}
// Fitted armor segments on an elliptical forearm. Rounded chamfered rims,
// inset faces and seams are real geometry, not painted rectangles.
struct Arm {
    Vec a{.30,-.48,-.42},b{.19,-.228,.30};
    Vec axis=unit(b-a),out=unit(Vec{1,0,0}-axis*axis.x),up=cross(axis,out);
    double radius(double t)const{return std::lerp(.119,.080,t)+.012*std::sin(Pi*t);}
    Vec p(double t,double angle,double lift=0)const{
        const double r=radius(t);
        return a+(b-a)*t+out*((r+lift)*std::cos(angle))+up*((r*.88+lift)*std::sin(angle));
    }
    Vec n(double angle)const{return unit(out*std::cos(angle)+up*(std::sin(angle)/.88));}
};
inline void shell(Geometry& g,const Arm& arm,double t0,double t1,double a0,double a1,double lift,Color color,int mat=SuitArmorTexture){
    constexpr int count=8; // independent curved panels, with sloped end rims
    const auto pt=[&](double t,double angle,double h){return Sample{arm.p(t,angle,h),arm.n(angle),angle*.12,t*.76};};
    const double dt=std::min(.022,(t1-t0)*.18),da=std::min(.075,(a1-a0)*.16);
    auto quad=[&](Sample a,Sample b,Sample c,Sample d,Color col){tri(g,a,b,c,mat,col);tri(g,a,c,d,mat,col);};
    for(int i=0;i<count;++i){
        const double u=double(i)/count,v=double(i+1)/count;
        const double a=std::lerp(a0,a1,u),b=std::lerp(a0,a1,v),c=std::lerp(a0+da,a1-da,u),d=std::lerp(a0+da,a1-da,v);
        quad(pt(t0,a,.002),pt(t0,b,.002),pt(t0+dt,d,lift),pt(t0+dt,c,lift),bevel);
        quad(pt(t0+dt,c,lift),pt(t0+dt,d,lift),pt(t1-dt,d,lift),pt(t1-dt,c,lift),color);
        quad(pt(t1-dt,c,lift),pt(t1-dt,d,lift),pt(t1,b,.002),pt(t1,a,.002),bevel);
    }
    for(int s:{0,1}){
        const double a=s?a1:a0,b=a+(s?-da:da);
        quad(pt(t0,a,.002),pt(t0+dt,b,lift),pt(t1-dt,b,lift),pt(t1,a,.002),bevel);
    }
}
inline void build(Geometry& g){
    const Arm arm;
    std::vector<Ring> sleeve;
    for(int i=0;i<=20;++i){const double t=i/20.;sleeve.push_back({arm.a+(arm.b-arm.a)*t,arm.radius(t),arm.radius(t)*.88});}
    sweep(g,sleeve,arm.out,SuitWeaveTexture,weave,32);
    // Offset interlocking carbon/ceramic panels across the visible forearm.
    for(int i=0;i<3;++i){
        const double t=.08+i*.215;
        shell(g,arm,t,t+.195,-.82,.73,.025,ceramic);
        shell(g,arm,t+.018,t+.177,.79,1.50,.015,carbon);
        shell(g,arm,t+.018,t+.177,-1.51,-.88,.016,carbon);
        shell(g,arm,t+.048,t+.145,-.60,.50,.031,carbon);
        // Low profile cooling ribs, molded into the central armor insert.
        for(int k=0;k<3;++k){const double q=t+.064+k*.023;
            shell(g,arm,q,q+.009,-.45,.35,.035,bevel);
        }
    }
    // Reinforced wrist gauntlet with four scalloped segments and a recessed
    // single status bar. The light is tiny relative to the black armor.
    for(int i=0;i<6;++i){const double a=-Pi+i*2*Pi/6;
        shell(g,arm,.755,.965,a+.035,a+2*Pi/6-.035,.022,ceramic);
    }
    shell(g,arm,.785,.935,-.50,.48,.032,carbon);
    shell(g,arm,.832,.852,-.32,.43,.036,violet,SuitLightTexture);
    // One restrained status mark on the outer/upper forearm, readable in hip view.
    shell(g,arm,.455,.48,.45,.79,.029,violet,SuitLightTexture);
    shell(g,arm,.864,.878,-.32,-.12,.035,violet,SuitLightTexture);
    for(double t:{.73,.975})for(int i=0;i<4;++i){
        const double a=-Pi+i*Pi*.5;
        shell(g,arm,t,t+.013,a+.03,a+Pi*.5-.03,.010,elastic,SuitRubberTexture);
    }
    // Wrist and palm are one connected sweep, widening into the hand and
    // narrowing at the knuckle line. The glove contacts the lower shroud.
    const Vec wrist{.177,-.220,.324};
    sweep(g,{{arm.b,.075,.066},{wrist,.063,.051},{{.155,-.189,.365},.064,.048},
        {{.146,-.163,.414},.080,.046},{{.147,-.137,.492},.086,.037},{{.145,-.116,.536},.043,.025}},
        {0,0,1},SuitWeaveTexture,weave,28);
    // Four fingers curl up the visible side, with differing lengths and a
    // deliberate gap between each. They stay outside the receiver panels.
    for(int i=0;i<4;++i){
        const double z=.365+i*.047,shorten=i==0?.014:i==3?.024:0.;
        const Vec a{.147,-.184,z},b{.195,-.146,z+.008},c{.199,-.098+shorten,z+.013},
            d{.177,-.050-shorten,z+.017},e{.145,-.038-shorten,z+.018};
        const double r=i==3?.018:.021;
        sweep(g,{{a,r*1.12,r},{b,r,r*.85},{c,r*.86,r*.77},{d,r*.72,r*.69},{e,r*.45,r*.48}},
            {0,0,1},SuitRubberTexture,elastic,16);
        // Low curved armor on each knuckle, with rubber between segments.
        for(const auto& pair:{std::pair{a,b},std::pair{b,c},std::pair{c,d}}){
            const Vec p=pair.first+(pair.second-pair.first)*.18+Vec{.010,0,0};
            const Vec q=pair.first+(pair.second-pair.first)*.77+Vec{.010,0,0};
            sweep(g,{{p,r*.82,r*.60},{(p+q)*.5,r*.97,r*.71},{q,r*.72,r*.52}},
                {0,0,1},SuitArmorTexture,ceramic,12);
        }
    }
    // Thumb wraps forward along the underside; it is not a fifth parallel finger.
    sweep(g,{{{.131,-.207,.448},.031,.026},{{.089,-.197,.513},.027,.023},
        {{.055,-.153,.542},.022,.019},{{.060,-.118,.550},.016,.014}},
        {1,0,0},SuitRubberTexture,elastic,18);
    sweep(g,{{{.138,-.217,.457},.022,.014},{{.099,-.208,.508},.025,.019},
        {{.073,-.184,.529},.016,.011}}, {1,0,0},SuitArmorTexture,ceramic,16);
}
}
