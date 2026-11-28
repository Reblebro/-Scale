#include "settlement.hpp"
#include "render.hpp"
#include "goblin-rig.hpp"
#include "witness-horror.hpp"

namespace settlement {
namespace {
constexpr Color olive{.35f,.40f,.19f},warmSkin{.42f,.40f,.22f},innerEar{.43f,.24f,.15f},
    silver{.67f,.63f,.57f},hairDark{.29f,.25f,.22f},leather{.19f,.10f,.064f},
    edgeLeather{.36f,.20f,.10f},bronze{.39f,.25f,.14f},gold{.67f,.42f,.22f},
    red{.24f,.042f,.032f},redEdge{.48f,.105f,.067f},pants{.19f,.14f,.10f},linen{.52f,.43f,.32f},
    amber{1.8f,.66f,.075f},dark{.045f,.030f,.018f};
constexpr int S=GoblinSkinTexture,H=GoblinHairTexture,L=GoblinLeatherTexture,B=GoblinBronzeTexture,
    C=GoblinClothTexture,A=GoblinAmberTexture,E=GoblinEyeTexture;
struct Pt {Vec p,n;Color c;double u,v;};
thread_local double meshQuality=1.;
void tri(Geometry& g,Pt a,Pt b,Pt c,int m){
    Vec n=cross(b.p-a.p,c.p-a.p);if(length(n)<1e-11)return;
    if(dot(n,a.n+b.n+c.n)<0)std::swap(b,c);
    for(auto q:{a,b,c})g.points.push_back({float(q.p.x),float(q.p.y),float(q.p.z),float(q.n.x),float(q.n.y),float(q.n.z),float(q.u),float(q.v),float(m),q.c.r,q.c.g,q.c.b});
}
Color tint(Color c,double s){return {float(c.r*s),float(c.g*s),float(c.b*s)};}
// Smooth parametric meshes, with normals derived from the actual surface.
template<class F> void surface(Geometry& g,int nu,int nv,int m,Color c,F f,bool wrap=true){
    nu=std::max(5,int(nu*meshQuality));nv=std::max(3,int(nv*meshQuality));
    auto point=[&](double u,double v){const Vec p=f(u,v);Vec n=unit(cross(f(u+.0001,v)-f(u-.0001,v),f(u,v+.0001)-f(u,v-.0001)));
        if(length(n)<.5)n={0,1,0};
        return Pt{p,n,c,u,v};};
    std::vector<Pt> grid((nu+1)*(nv+1));
    for(int j=0;j<=nv;++j)for(int i=0;i<=nu;++i)grid[j*(nu+1)+i]=point(double(i)/nu,double(j)/nv);
    (void)wrap;
    for(int j=0;j<nv;++j)for(int i=0;i<nu;++i){auto a=grid[j*(nu+1)+i],b=grid[j*(nu+1)+i+1],d=grid[(j+1)*(nu+1)+i],e=grid[(j+1)*(nu+1)+i+1];tri(g,a,b,e,m);tri(g,a,e,d,m);}
}
void oval(Geometry& g,Vec p,Vec r,int m,Color c,int n=24,int k=14){
    surface(g,n,k,m,c,[=](double u,double v){double a=2*Pi*u,b=Pi*(v-.5);return p+Vec{r.x*std::sin(a)*std::cos(b),r.y*std::sin(b),r.z*std::cos(a)*std::cos(b)};});
}
struct Section {double y,x,z,cx,cz;};
void loft(Geometry& g,std::initializer_list<Section> seq,int m,Color c,int sides=24,int steps=3,double folds=0){
    const std::vector<Section> q(seq);const int count=int(q.size())-1;
    surface(g,sides,count*steps,m,c,[=](double u,double v){double t=std::clamp(v,0.,1.)*count;int j=std::min(int(t),count-1);t-=j;double a=u*2*Pi;
        const auto p=q[j],r=q[j+1];double rx=std::lerp(p.x,r.x,t),rz=std::lerp(p.z,r.z,t);
        double ripple=1+folds*(.60*std::sin(5*a+v*12)+.40*std::sin(9*a-v*15));
        return Vec{std::lerp(p.cx,r.cx,t)+rx*ripple*std::sin(a),std::lerp(p.y,r.y,t),std::lerp(p.cz,r.cz,t)+rz*ripple*std::cos(a)};});
}
Vec curve(Vec a,Vec b,Vec c,Vec d,double t){return a*std::pow(1-t,3)+b*(3*t*(1-t)*(1-t))+c*(3*t*t*(1-t))+d*(t*t*t);}
// Swept elliptical locks, fingers, trim, and narrow folds. No sphere chains.
void sweep(Geometry& g,Vec a,Vec b,Vec c,Vec d,double r0,double r1,double flat,int m,Color color,int sides=12,int steps=12){
    surface(g,sides,steps,m,color,[=](double u,double v){Vec p=curve(a,b,c,d,v),t=unit(curve(a,b,c,d,v+.0001)-curve(a,b,c,d,v-.0001));
        Vec x=unit(cross(t,std::abs(t.z)<.92?Vec{0,0,1}:Vec{0,1,0})),z=cross(t,x);
        double r=std::max(.0001,std::lerp(r0,r1,v));double ang=u*2*Pi;return p+x*(r*std::cos(ang))+z*(r*flat*std::sin(ang));});
}
void rod(Geometry& g,Vec a,Vec b,double r,int m,Color c,int n=12){g.tube(a,b,r,m,c,std::max(4,int(n*meshQuality)));}
void buckle(Geometry& g,Vec p,double w,double h,int n=12){
    const Vec a=p+Vec{-w,-h,0},b=p+Vec{w,-h,0},c=p+Vec{w,h,0},d=p+Vec{-w,h,0};
    for(auto e:std::array<std::pair<Vec,Vec>,5>{{{a,b},{b,c},{c,d},{d,a},{p-Vec{0,h,0},p+Vec{0,h,0}}}})rod(g,e.first,e.second,.0035,B,gold,n);
}
void gear(Geometry& g,Vec p,double r,int n=16){
    for(int i=0;i<n;i++){double a=2*Pi*i/n,b=2*Pi*(i+1)/n;auto pt=[&](double f,double t){return p+Vec{r*f*std::cos(t),r*f*std::sin(t),0};};
        g.quad(pt(.58,a),pt(1,a),pt(1,b),pt(.58,b),B,gold);if(i%2==0)g.quad(pt(.91,a),pt(1.22,a),pt(1.22,b),pt(.91,b),B,gold);}
    oval(g,p+Vec{0,0,.003},{r*.30,r*.30,.007},B,bronze,12,8);
}
void strap(Geometry& g,Vec a,Vec b,double w){Vec side=unit(cross(b-a,{0,0,1}))*w;g.quad(a-side,b-side,b+side,a+side,L,edgeLeather);
    for(double s:{-1.,1.})rod(g,a+side*s+Vec{0,0,.001},b+side*s+Vec{0,0,.001},.0015,B,bronze,6);
    for(int i=1;i<9;i++)oval(g,a+(b-a)*(i/10.)+Vec{0,0,.003},{.0025,.0025,.0015},B,gold,8,5);
}
double bump(double x,double y,double cx,double cy,double rx,double ry){return std::exp(-2*(std::pow((x-cx)/rx,2)+std::pow((y-cy)/ry,2)));}
// Face profile is hand-authored in anatomical cross sections. Eyes, brow,
// nose, chin, cheek fat and lips are geometry, not a decal on a sphere.
Vec facePoint(double u,double v){
    constexpr std::array<Section,12> q={{{1.130,.008,.012,0,.021},{1.141,.039,.038,0,.030},{1.159,.067,.060,0,.017},{1.183,.078,.071,0,.005},{1.209,.085,.076,0,.001},{1.231,.093,.077,0,0},{1.254,.092,.075,0,-.002},{1.279,.088,.074,0,-.004},{1.306,.085,.069,0,-.008},{1.331,.067,.055,0,-.012},{1.346,.038,.033,0,-.013},{1.353,.001,.001,0,-.013}}};
    const double t=std::clamp(v,0.,1.)*(q.size()-1);int i=std::min(int(t),int(q.size())-2);double h=t-i;
    auto cat=[&](auto field){double a=q[std::max(0,i-1)].*field,b=q[i].*field,c=q[i+1].*field,d=q[std::min(int(q.size())-1,i+2)].*field;return .5*(2*b+(c-a)*h+(2*a-5*b+4*c-d)*h*h+(3*b-a-3*c+d)*h*h*h);};
    double a=2*Pi*u,y=std::lerp(q[i].y,q[i+1].y,h),x=cat(&Section::x)*std::sin(a),f=std::cos(a),z=cat(&Section::cz)+cat(&Section::z)*f;
    if(f>0){double k=std::clamp(f*1.6,0.,1.);for(double s:{-1.,1.})z+=k*(.020*bump(x,y,s*.055,1.227,.036,.020)-.015*bump(x,y,s*.040,1.256,.027,.016)+.018*bump(x,y,s*.039,1.273,.038,.012)-.010*bump(x,y,s*.066,1.196,.021,.027));
        z+=k*(.034*bump(x,y,0,1.240,.016,.034)+.035*bump(x,y,0,1.226,.021,.012)+.009*bump(x,y,0,1.189,.043,.009)+.012*bump(x,y,0,1.155,.036,.015)-.004*bump(x,y,0,1.181,.035,.004));}
    return {x,y,z};
}
Vec faceAt(double x,double y){double v0=0,v1=1;for(int j=0;j<22;j++){double m=(v0+v1)*.5;if(facePoint(0,m).y<y)v0=m;else v1=m;}double v=(v0+v1)*.5,u0=-.25,u1=.25;for(int i=0;i<24;i++){double m=(u0+u1)*.5;if(facePoint(m,v).x<x)u0=m;else u1=m;}return facePoint((u0+u1)*.5,v);}
void face(Geometry& g,bool low){
    const auto first=g.points.size();surface(g,low?40:112,low?26:84,S,olive,facePoint);
    for(std::size_t i=first;i<g.points.size();++i){auto& p=g.points[i];double x=p.x,y=p.y;
        double shade=1.-.27*(bump(x,y,-.04,1.256,.028,.018)+bump(x,y,.04,1.256,.028,.018))
            -.12*(bump(x,y,-.065,1.191,.030,.030)+bump(x,y,.065,1.191,.030,.030));
        double warmth=.11*(bump(x,y,-.05,1.229,.041,.025)+bump(x,y,.05,1.229,.041,.025))+.09*bump(x,y,0,1.225,.030,.019);
        if(p.z<0){shade=1;warmth=0;}p.r=float(olive.r*(shade+warmth));p.g=float(olive.g*(shade-.3*warmth));p.b=float(olive.b*(shade+.4*warmth));
    }
    // Almond-shaped visible eye surface, seated between upper and lower lids.
    for(double s:{-1.,1.}){
        Vec c=faceAt(s*.040,1.255);c.z+=.002;
        auto eye=[&](double u,double v){double x=(u-.5)*.047,shape=std::pow(std::max(0.,1-std::abs(2*u-1)),.72),y=(v-.5)*.020*shape;
            Vec p=faceAt(c.x+x,c.y+y+s*x*.16);p.z+=.002+.005*shape*std::sin(Pi*v);return p;};
        surface(g,low?12:32,low?6:16,E,{.24f,.20f,.11f},eye,false);
        Vec iris=eye(.51,.52);iris.z+=.001;
        oval(g,iris,{.0082,.0082,.0023},E,{.23f,.105f,.022f},24,12);
        oval(g,iris+Vec{0,0,.0011},{.0067,.0067,.0021},E,{.68f,.32f,.038f},24,12);
        if(!low)for(int i=0;i<24;i++){double a=2*Pi*i/24;Vec d{std::cos(a),std::sin(a),0};rod(g,iris+d*.004+Vec{0,0,.0030},iris+d*.0065+Vec{0,0,.0024},.00035,E,i%3==0?Color{.83f,.49f,.10f}:Color{.34f,.14f,.025f},5);}
        oval(g,iris+Vec{0,0,.0024},{.0037,.0040,.0013},E,{.019f,.012f,.008f},16,8);
        oval(g,iris+Vec{-.002,.002,.0033},{.0015,.0014,.0005},E,{.93f,.79f,.47f},8,6);
        for(int side:{0,1})for(int i=0;i<18;i++){Vec a=eye(i/18.,side),b=eye((i+1)/18.,side);rod(g,a,b,side?.0021:.0014,S,side?Color{.27f,.23f,.12f}:warmSkin,8);}
        Vec a=faceAt(s*.017,1.274),b=faceAt(s*.035,1.282),d=faceAt(s*.067,1.274);a.z+=.005;b.z+=.006;d.z+=.004;
        sweep(g,a,b,b,d,.0063,.0012,.74,H,{.12f,.085f,.047f},12,16);
        if(!low)for(int k=0;k<14;k++){double t=k/14.;Vec p=curve(a,b,b,d,t);sweep(g,p-Vec{0,.0015,0},p+Vec{s*.001,.003,.001},p+Vec{s*.003,.003,.001},p+Vec{s*.004,.001,0},.00065,.00008,1,H,{.18f,.12f,.06f},5,5);}
        for(int i=0;i<18;i++){double t=i/18.,q=(i+1)/18.;auto crease=[&](double t){double x=s*(.016+.048*t);Vec p=faceAt(x,1.240-.002*std::sin(Pi*t));p.z+=.001;return p;};rod(g,crease(t),crease(q),.0007,S,{.25f,.27f,.115f},6);}
        Vec nostril=faceAt(s*.013,1.220);nostril.z+=.0008;oval(g,nostril,{.0049,.0022,.0015},E,dark,14,9);
        Vec ala=faceAt(s*.019,1.225);ala.z+=.0007;oval(g,ala,{.008,.006,.004},S,warmSkin,18,12);
        // Small freckles and pores concentrated over the cheekbones.
        if(!low)for(int i=0;i<24;i++){auto k=mix(unsigned(i*519+(s>0?713:13)));double x=s*(.036+.033*(k&255)/255.),y=1.218+.022*((k>>8)&255)/255.;Vec p=faceAt(x,y);p.z+=.0005;oval(g,p,{.0008,.0007,.00045},S,{.26f,.22f,.09f},6,4);}
        // The pointed ear has a raised cartilaginous rim and recessed bowl.
        Vec base{s*.076,1.240,-.005},tip{s*.150,1.281,-.030};
        auto ear=[&](double u,double v){double a=-s*u*2*Pi;double t=v;Vec p=base+(tip-base)*t;
            double r=std::sin(Pi*t)*.026+.016*(1-t);return p+Vec{0,r*std::sin(a),r*.40*std::cos(a)-.007*std::sin(Pi*t)*std::max(0.,std::cos(a))};};
        surface(g,24,15,S,warmSkin,ear);
        sweep(g,base+Vec{0,.020,.006},base+Vec{s*.028,.031,.010},tip+Vec{-s*.022,.002,.010},tip,.006,.0005,.8,S,olive,10,16);
        sweep(g,base+Vec{0,-.015,.009},base+Vec{s*.027,-.018,.014},tip+Vec{-s*.022,-.012,.008},tip,.008,.0005,.65,S,innerEar,12,16);
        if(s<0){oval(g,base+Vec{s*.017,-.015,.013},{.003,.003,.003},B,gold,10,6);}
    }
    // Modeled upper and lower vermilion, shaped around the closed seam.
    for(int side:{-1,1})surface(g,36,9,S,side>0?Color{.37f,.32f,.16f}:Color{.43f,.37f,.20f},[=](double u,double v){
        double x=(u-.5)*.066,w=std::pow(std::max(0.,1.-std::abs(2*u-1)),.65),y=1.185+.002*std::cos(x*85)+side*.007*w*v;
        Vec p=faceAt(x,y);p.z+=.0012+.0032*w*std::sin(Pi*v);return p;},false);
    // Closed stern lips: cupid's bow and dark seam rather than a cartoon grin.
    for(int i=0;i<20;i++){double x=-.031+.062*i/20.,xx=-.031+.062*(i+1)/20.;auto mouth=[&](double a){Vec p=faceAt(a,1.185+.002*std::cos(a*85));p.z+=.001;return p;};
        Vec a=mouth(x),b=mouth(xx);rod(g,a,b,.0012,E,{.17f,.095f,.053f},8);}
    // Silver swept hair: scalp and individual sculpted, overlapping locks.
    surface(g,low?24:64,low?10:28,H,tint(silver,.75),[](double u,double v){double a=-u*2*Pi,t=v*(1.10+.42*(1-std::cos(a))+.06*std::sin(a*7));return Vec{.094*std::sin(a)*std::sin(t),1.296+.080*std::cos(t),-.015+.081*std::cos(a)*std::sin(t)};});
    const int locks=low?18:40;
    for(int i=0;i<locks;i++){double a=2*Pi*i/locks;auto k=mix(i*173u+0x834u);double f=(k&255)/255.;
        Vec p{.065*std::sin(a),1.327+.018*f,-.013+.052*std::cos(a)};
        Vec b=p+Vec{.014,.044+.012*f,.018},c{-.027+.082*std::sin(a),1.34+.021*f,.012+.076*std::cos(a)},d{-.030+.10*std::sin(a),1.276+.047*f,-.015+.09*std::cos(a)};
        if(std::cos(a)>.25){c={-.055+.048*std::sin(a),1.363+.016*f,.077};d={-.079+.043*std::sin(a),1.286+.028*f,.076};}
        Color col=tint(silver,.73+.34*f);sweep(g,p,b,c,d,.013+.008*f,.0008,.52,H,col,low?7:13,low?9:21);
        if(!low)for(int k=0;k<2;k++){Vec off{(k-1)*.006,.002,.003};sweep(g,p+off,b+off,c+off,d,.0015,.00018,.75,H,tint(col,1.13),5,17);}
    }
    // Tapered flyaways break up the groom's contour and do not follow a grid.
    if(!low)for(int i=0;i<24;i++){double t=i/24.,a=t*2*Pi;Vec p{.065*std::sin(a),1.347,-.012+.060*std::cos(a)};
        Vec q=p+Vec{-.019,.054,.019},r=p+Vec{-.051,.040,.023},d=p+Vec{-.069,.004,.015};sweep(g,p,q,r,d,.0014,.00014,.7,H,tint(silver,.82+.30*t),6,16);}
    // Broad locks flow across the frontal hairline and down over the temple.
    for(int j=0;j<(low?8:19);j++){
        double t=double(j)/(low?7:18),f=.5+.5*std::sin(j*2.17);
        Vec a{.052+.022*t,1.335+.020*t,-.011+.026*t},b{.031,1.391+.012*f,.059},c{-.035-.045*t,1.349+.009*f,.093},d{-.084+.024*t,1.281+.030*t+.009*f,.066+.014*t};
        sweep(g,a,b,c,d,.014+.009*f,.0007,.62,H,tint(silver,.82+.24*f),12,20);
        if(!low)sweep(g,a+Vec{0,.004,.006},b+Vec{0,.004,.006},c+Vec{0,.004,.006},d,.002,.00015,.7,H,tint(silver,1.2),6,18);
    }
}
void pauldron(Geometry& g,Vec p,int s,bool low){
    for(int layer=0;layer<3;layer++){
        const Vec center=p+Vec{s*(.005+layer*.008),-layer*.029,0};
        auto plate=[=](double u,double v){double a=s*(u-.5)*Pi*1.36,b=.18+v*1.45;
            double rx=.079-layer*.007,rz=.088-layer*.006;
            return center+Vec{s*(rx*std::cos(a)*std::sin(b)-.013),.083*std::cos(b),rz*std::sin(a)*std::sin(b)};};
        surface(g,low?13:28,low?6:15,B,tint(bronze,.88+layer*.08),plate,false);
        for(int i=0;i<20;i++){Vec a=plate(i/20.,1),b=plate((i+1)/20.,1);rod(g,a,b,.0034,B,gold,8);}
        for(int i=1;i<8;i++){Vec a=plate(i/8.,.90);oval(g,a,{.0034,.0034,.0034},B,gold,8,6);}
        // Raised etched border running through the curved plate itself.
        for(int i=0;i<18;i++){Vec a=plate(i/18.,.74),b=plate((i+1)/18.,.74);rod(g,a,b,.0013,B,edgeLeather,6);}
        if(!low)for(int i=0;i<7;i++){double u=.15+i*.115;Vec a=plate(u,.40),b=plate(u+.027,.54),c=plate(u,.67),d=plate(u-.027,.54);
            for(auto line:std::array<std::pair<Vec,Vec>,4>{{{a,b},{b,c},{c,d},{d,a}}})rod(g,line.first,line.second,.0009,B,tint(gold,.73),5);}
    }
}
void ring(Geometry& g,Vec p,double rx,double rz,double thick,int mat,Color c){loft(g,{{p.y-thick*.5,rx,rz,p.x,p.z},{p.y+thick*.5,rx,rz,p.x,p.z}},mat,c,24,1);}
void spear(Geometry& g,Vec grip,bool low){
    Vec b{grip.x,.046,grip.z},t{grip.x,1.405,grip.z};rod(g,b,t,.014,L,edgeLeather,18);
    for(double y:{.09,.37,.66,.91,1.22,1.35}){ring(g,{grip.x,y,grip.z},.019,.019,.017,B,gold);
        for(double d:{-.008,.008})ring(g,{grip.x,y+d,grip.z},.020,.020,.003,B,bronze);}
    if(!low)for(int i=0;i<280;i++){auto wrap=[&](double n){double a=n*Pi/4;return Vec{grip.x+.0145*std::cos(a),.12+n*.0043,grip.z+.0145*std::sin(a)};};rod(g,wrap(i),wrap(i+1),.0011,L,dark,5);}
    const Vec c{grip.x,1.40,grip.z};
    // Angular bronze spear head with open channels around its glowing crystal.
    auto p=[&](double x,double y,double z){return c+Vec{x,y,z};};
    for(double s:{-1.,1.}){Vec tip=p(0,.205,0),a=p(s*.026,.09,0),b=p(s*.021,.062,0),d=p(s*.060,.022,0),e=p(s*.034,-.012,0),f=p(s*.013,.028,0),h=p(s*.010,.092,0);
        for(auto q:std::array<std::pair<Vec,Vec>,6>{{{tip,a},{a,b},{b,d},{d,e},{e,f},{f,h}}}){rod(g,q.first,q.second,.005,B,gold,10);rod(g,q.first-Vec{0,0,.004},q.second-Vec{0,0,.004},.004,B,bronze,8);}
        g.triangle(tip,a,h+Vec{0,0,.005},B,bronze);g.quad(b,d,e,f+Vec{0,0,.005},B,bronze);}
    Vec top=p(0,.124,0),bottom=p(0,-.012,0);std::array<Vec,4> mid={p(.014,.038,0),p(0,.038,.015),p(-.014,.038,0),p(0,.038,-.015)};
    for(int i=0;i<4;i++){g.triangle(top,mid[i],mid[(i+1)%4],A,amber);g.triangle(bottom,mid[(i+1)%4],mid[i],A,tint(amber,.8));}
    gear(g,c-Vec{0,.017,-.020},.023);oval(g,b,{.016,.045,.016},B,bronze,12,8);
    for(double s:{-1.,1.}){
        rod(g,p(s*.019,.015,.007),p(s*.011,.112,.007),.0024,A,amber,8);
        for(int i=0;i<4;i++){double y=.027+i*.021;rod(g,p(s*.028,y,.009),p(s*.018,y+.007,.009),.0019,A,amber,6);}
    }
    for(double y:{.91,1.22,1.35})for(int i=0;i<4;i++){
        double a=i*Pi/2;Vec c2{grip.x+.020*std::cos(a),y,grip.z+.020*std::sin(a)};
        rod(g,c2-Vec{0,.005,0},c2+Vec{0,.005,0},.003,A,amber,8);
    }
}
void sword(Geometry& g,Vec grip){
    auto p=[&](double x,double y,double z){return grip+Vec{x,y,z};};
    rod(g,p(0,-.09,0),p(0,.045,0),.020,L,edgeLeather,16);
    for(int i=0;i<8;i++)ring(g,p(0,-.080+i*.016,0),.021,.021,.003,B,bronze);
    oval(g,p(0,-.103,0),{.027,.027,.027},B,gold,16,12);
    rod(g,p(-.092,.055,0),p(.092,.055,0),.013,B,bronze,12);
    gear(g,p(0,.055,.017),.044,20);oval(g,p(0,.055,.023),{.019,.019,.009},A,amber,16,10);
    for(double s:{-1.,1.})for(double f:{-1.,1.}){
        Vec a=p(s*.041,.08,0),b=p(s*.034,.49,0),tip=p(0,.61,0),ridge=p(0,.49,f*.011),root=p(0,.08,f*.019);
        g.quad(a,b,ridge,root,B,tint(gold,1.12));g.triangle(b,tip,ridge,B,tint(gold,1.22));
        for(int i=0;i<7;i++){double y=.13+i*.049;rod(g,p(0,y,f*.016),p(s*.014,y+.019,f*.016),.002,A,amber,6);}
    }
}
void crossbow(Geometry& g,Vec grip){
    auto p=[&](double x,double y,double z){return grip+Vec{x,y,z};};
    g.box(p(-.038,-.018,-.19),p(.038,.034,.28),L,edgeLeather);
    g.box(p(-.049,.024,-.15),p(.049,.065,.23),B,bronze);
    for(double s:{-1.,1.}){
        Vec old=p(0,.035,.25);
        for(int i=1;i<=12;i++){double t=i/12.;Vec next=p(s*.29*std::sin(t*Pi*.5),.04,.25-.19*t*t);
            rod(g,old,next,.018,B,bronze,12);rod(g,old+Vec{0,.016,0},next+Vec{0,.016,0},.004,B,gold,8);old=next;}
        rod(g,old,p(0,.072,-.07),.0027,C,linen,8);
        gear(g,p(s*.050,.043,.13),.025,16);
        rod(g,p(s*.052,.04,-.095),p(s*.052,.04,.08),.008,A,amber,10);
    }
    rod(g,p(0,.079,-.12),p(0,.079,.35),.005,B,gold,8);
    g.triangle(p(-.024,.079,.32),p(.024,.079,.32),p(0,.079,.395),B,gold);
    g.triangle(p(0,.059,.32),p(0,.099,.32),p(0,.079,.395),B,gold);
    rod(g,p(0,-.02,-.07),p(0,-.083,-.085),.017,L,leather,12);
}
struct Frame {Vec x{1,0,0},y{0,1,0},z{0,0,1},p{};Vec dir(Vec v)const{return x*v.x+y*v.y+z*v.z;}Vec at(Vec v)const{return p+dir(v);}};
Frame rotation(Vec e){double a=std::cos(e.x),b=std::sin(e.x),c=std::cos(e.y),d=std::sin(e.y),f=std::cos(e.z),g=std::sin(e.z);auto r=[&](Vec q){q={q.x,a*q.y-b*q.z,b*q.y+a*q.z};q={c*q.x+d*q.z,q.y,-d*q.x+c*q.z};return Vec{f*q.x-g*q.y,g*q.x+f*q.y,q.z};};return {r({1,0,0}),r({0,1,0}),r({0,0,1}),{}};}
}
int GoblinRig::add(int parent,Vec pivot){bones.push_back({parent,pivot,{}});return int(bones.size())-1;}
void GoblinRig::build(bool low,int weaponClass){
    role=std::clamp(weaponClass,0,2);armament.points.clear();
    meshQuality=low?.48:.76;
    bones.clear();pelvis=add(-1,{0,.66,0});spine=add(pelvis,{0,.76,0});chest=add(spine,{0,.91,0});neck=add(chest,{0,1.105,0});head=add(neck,{0,1.15,0});scarf=add(chest,{0,1.04,.08});tabard=add(pelvis,{0,.71,.10});
    for(int i=0;i<2;i++){double s=i?1:-1;auto& q=sides[i];q.shoulder=add(chest,{s*.14,1.02,0});q.upper=add(q.shoulder,{s*.17,.99,0});q.elbow=add(q.upper,{s*.212,.81,.015});q.wrist=add(q.elbow,{s*(i?.28:.237),i?.88:.65,i?.15:.035});q.hip=add(pelvis,{s*.081,.65,0});q.knee=add(q.hip,{s*.092,.36,.012});q.ankle=add(q.knee,{s*.098,.095,0});q.toe=add(q.ankle,{s*.098,.043,.071});
        for(int d=0;d<5;d++){Vec base=bones[q.wrist].pivot+Vec{(d-2)*.013,-.040,i?.020:.013};if(d==4)base=bones[q.wrist].pivot+Vec{-s*.031,-.014,.023};
            if(i==1 && d!=4)base=bones[q.wrist].pivot+Vec{-.017,-.039+d*.014,.022};
            q.digit[d][0]=add(q.wrist,base);q.digit[d][1]=add(q.digit[d][0],base+(i==1?Vec{.011,0,.021}:Vec{0,-.019,.004}));q.digit[d][2]=add(q.digit[d][1],base+(i==1?Vec{.029,0,.020}:Vec{0,-.031,.011}));}
    }
    auto& torso=bones[chest].mesh;loft(torso,{{.76,.115,.075,0,0},{.85,.127,.088,0,0},{.96,.165,.088,0,0},{1.035,.15,.077,0,0},{1.075,.065,.048,0,0}},L,leather,low?18:40,4,.05);
    loft(bones[spine].mesh,{{.65,.126,.079,0,0},{.75,.121,.084,0,0},{.85,.126,.088,0,0}},L,leather,32,4,.06);
    loft(bones[pelvis].mesh,{{.58,.119,.081,0,0},{.65,.135,.090,0,0},{.73,.122,.083,0,0}},C,pants,32,4,.06);
    loft(bones[neck].mesh,{{1.044,.049,.042,0,0},{1.105,.042,.038,0,0},{1.166,.055,.044,0,.007}},S,olive,24,5);
    face(bones[head].mesh,low);
    // Folded burgundy cowl and long asymmetric embroidered tabard.
    loft(bones[scarf].mesh,{{.958,.171,.123,0,.008},{1.006,.162,.113,0,.003},{1.055,.124,.083,0,0},{1.09,.062,.051,0,0}},C,red,36,6,.16);
    surface(bones[scarf].mesh,24,22,C,red,[&](double u,double v){double x=(.5-u)*(.27-.11*v);return Vec{x,1.045-v*.164-.012*std::sin(u*14)*v,.136+.022*std::sin(u*Pi)+.007*std::sin(v*21+u*7)};},false);
    // Two additional overlapping scarf folds, with weathered stitched edges.
    for(int j=0;j<3;j++){Vec a{-.135,1.015-j*.015,.125},b{-.064,1.00-j*.022,.155},c{.045,.94-j*.015,.169},d{.120,1.003-j*.024,.128};
        sweep(bones[scarf].mesh,a,b,c,d,.009,.004,.58,C,tint(red,1.18+j*.12),12,24);}
    auto& tab=bones[tabard].mesh;auto fabric=[](double u,double v){return Vec{(.5-u)*(.18-.035*v),.705-v*.30+.013*std::cos(u*23)*v*v,.102+.013*std::sin(u*16+v*4)+.01*v};};
    surface(tab,24,30,C,red,fabric,false);for(double u:{.04,.96})for(int i=0;i<24;i++)rod(tab,fabric(u,i/24.)+Vec{0,0,.002},fabric(u,(i+1)/24.)+Vec{0,0,.002},.0016,B,gold,6);gear(tab,{0,.505,.121},.028,16);
    auto& waist=bones[pelvis].mesh;for(double y:{.65,.70}){ring(waist,{0,y,0},.143,.099,.033,L,edgeLeather);ring(waist,{0,y-.015,0},.146,.101,.003,B,bronze);buckle(waist,{.034,y,.106},.023,.016);}
    strap(torso,{-.132,1.015,.080},{.115,.79,.093},.022);buckle(torso,{-.018,.91,.115},.019,.014);
    for(double y:{.83,.88,.94}){buckle(torso,{.026,y,.097},.013,.010,8);}
    // Saddle stitching and seams down each side of the fitted leather coat.
    for(double s:{-1.,1.})for(int i=0;i<25;i++){
        double y=.785+i*.0075,x=s*(.074+.026*(y-.785)/.188),z=.077-.01*std::abs(y-.88);
        Vec a{x,y,z},b{x+s*.004,y+.003,z+.002};rod(torso,a,b,.00085,L,edgeLeather,5);
    }
    for(double s:{-1.,1.}){Vec a{s*.11,1.006,.075},b{s*.08,.94,.091};strap(torso,a,b,.009);}
    for(double s:{-1.,1.}){Vec p{s*.131,.61,.075};oval(waist,p,{.047,.064,.032},L,leather,24,14);
        surface(waist,16,12,L,edgeLeather,[=](double u,double v){double x=(u-.5)*.088,y=.045-v*.052;return p+Vec{x,y,.031+.006*std::sin(Pi*u)*std::sin(Pi*v)-.006*std::abs(2*u-1)};},false);
        strap(waist,p+Vec{0,.052,.035},p+Vec{0,-.040,.035},.011);buckle(waist,p+Vec{0,-.015,.042},.010,.012);
        if(!low)for(double sign:{-1.,1.})for(int j=0;j<10;j++){Vec a=p+Vec{sign*.037,-.04+j*.008,.022};rod(waist,a,a+Vec{-sign*.004,.002,.001},.0009,L,edgeLeather,5);}}

    if(role==1){
        Vec a{-.10,.73,-.105},b{-.15,1.18,-.115};rod(torso,a,b,.043,L,leather,20);
        for(double t:{.08,.88}){Vec c=a+(b-a)*t;rod(torso,c-Vec{0,.012,0},c+Vec{0,.012,0},.046,B,bronze,16);}
        for(int i=0;i<5;i++){Vec p=b+Vec{.024*std::cos(i*2.4),-.04,.022*std::sin(i*2.4)};Vec q=p+Vec{-.025,.16+.014*i,0};rod(torso,p,q,.004,L,edgeLeather,8);torso.triangle(q-Vec{.012,.045,0},q+Vec{.012,-.045,0},q+Vec{0,.02,0},C,linen);}
    }else if(role==2){Vec a{-.16,.70,-.035},b{-.20,.19,-.065};rod(waist,a,b,.032,L,leather,14);rod(waist,b,b+Vec{0,.06,0},.034,B,bronze,12);}
    Vec vial{-.089,.585,.113};rod(waist,vial-Vec{0,.047,0},vial+Vec{0,.038,0},.018,A,tint(amber,.7),16);for(double y:{-.047,.038})ring(waist,vial+Vec{0,y,0},.024,.024,.013,B,gold);for(double x:{-.017,.017})rod(waist,vial+Vec{x,-.045,.010},vial+Vec{x,.038,.010},.003,B,bronze,8);
    for(int i=0;i<2;i++){
        double s=i?1:-1;auto& q=sides[i];Vec sh=bones[q.upper].pivot,el=bones[q.elbow].pivot,wr=bones[q.wrist].pivot,hp=bones[q.hip].pivot,kn=bones[q.knee].pivot,an=bones[q.ankle].pivot;
        auto& shoulder=bones[q.shoulder].mesh;pauldron(shoulder,sh+Vec{0,.012,0},int(s),low);gear(shoulder,sh+Vec{-s*.035,.017,.098},.027,16);
        auto& upper=bones[q.upper].mesh;sweep(upper,sh,sh+(el-sh)*.28,sh+(el-sh)*.72,el,.060,.039,.84,L,leather,24,14);oval(upper,sh,{.051,.052,.052},L,leather,20,12);
        Vec cuff=sh+(el-sh)*.75;sweep(upper,cuff,cuff+(el-cuff)*.3,cuff+(el-cuff)*.7,el,.053,.046,.92,C,linen,28,12);
        auto& fore=bones[q.elbow].mesh;oval(fore,el,{.039,.039,.039},S,olive,24,12);sweep(fore,el,el+(wr-el)*.33,el+(wr-el)*.66,wr,.037,.027,.83,S,olive,24,15);
        Vec a=el+(wr-el)*.34,b=el+(wr-el)*.90;sweep(fore,a,a+(b-a)*.25,a+(b-a)*.72,b,.044,.040,.88,L,leather,26,10);
        for(double f:{.34,.57,.91}){Vec c=el+(wr-el)*f;rod(fore,c-(wr-el)*.035,c+(wr-el)*.035,.045,B,bronze,24);for(double z:{-.019,.019})oval(fore,c+Vec{s*.037,0,z},{.003,.003,.003},B,gold,8,5);}
        auto& hand=bones[q.wrist].mesh;oval(hand,wr+Vec{0,-.017,.006},{.034,.034,.024},L,leather,24,15);oval(hand,wr+Vec{0,-.017,.024},{.028,.026,.009},L,edgeLeather,20,10);
        for(int d=0;d<5;d++)for(int j=0;j<3;j++){int id=q.digit[d][j];Vec p=bones[id].pivot,end=j<2?bones[q.digit[d][j+1]].pivot:p+(i==1?Vec{.005,0,-.013}:Vec{0,-.010,.008});double r=d==4?.008:.0065;
            auto& digit=bones[id].mesh;oval(digit,p,{r,r,r},j==0?L:S,j==0?leather:olive,12,8);sweep(digit,p,p+(end-p)*.33,p+(end-p)*.66,end,r,r*.78,.86,j==0?L:S,j==0?leather:olive,12,6);if(j==2)oval(digit,end,{r*.75,r*.66,r*.76},S,olive,10,6);}
        if(i==1){ // grip and staff share the wrist's full transform
            const Vec grip=wr+Vec{.008,-.020,.030};
            if(role==0)spear(armament,grip,low);
            else if(role==1)crossbow(armament,grip);
            else sword(armament,grip);
        }
        auto& thigh=bones[q.hip].mesh;loft(thigh,{{kn.y,.057,.052,kn.x,kn.z},{kn.y+.045,.073,.059,kn.x,kn.z},{kn.y+.10,.075,.061,std::lerp(kn.x,hp.x,.3),.006},{hp.y-.045,.072,.063,hp.x,0},{hp.y,.063,.061,hp.x,0}},C,pants,32,6,.10);oval(thigh,kn,{.061,.055,.061},C,pants,24,12);
        // Leather thigh strap, small buckle and rivets.
        Vec strapC=hp+(kn-hp)*.31;rod(thigh,strapC-Vec{0,.012,0},strapC+Vec{0,.012,0},.078,L,edgeLeather,24);buckle(thigh,strapC+Vec{.017,0,.078},.015,.012);
        auto& shin=bones[q.knee].mesh;sweep(shin,kn,kn+(an-kn)*.26,kn+(an-kn)*.73,an,.056,.044,.88,L,leather,28,16);
        for(double f:{.13,.79}){Vec p=kn+(an-kn)*f;ring(shin,p,.060,.054,.027,L,edgeLeather);ring(shin,p-Vec{0,.012,0},.061,.055,.004,B,bronze);buckle(shin,p+Vec{.018,0,.058},.013,.011);}
        for(int lace=0;lace<6;lace++){double y=.135+lace*.028;for(double sign:{-1.,1.}){Vec p{s*.098+sign*.026,y,.046},r{s*.098-sign*.026,y+.025,.049};rod(shin,p,r,.0024,L,edgeLeather,8);oval(shin,p,{.0038,.0038,.002},B,gold,9,6);}}
        auto& boot=bones[q.ankle].mesh;loft(boot,{{.009,.050,.071,an.x,.026},{.025,.055,.074,an.x,.026},{.058,.049,.071,an.x,.019},{.094,.044,.043,an.x,0},{.115,.043,.042,an.x,0}},L,leather,30,4);auto& toe=bones[q.toe].mesh;loft(toe,{{.012,.051,.064,an.x,.091},{.026,.054,.063,an.x,.090},{.046,.052,.059,an.x,.087},{.060,.040,.046,an.x,.077},{.069,.022,.027,an.x,.071}},L,edgeLeather,30,4);
        loft(toe,{{.006,.052,.065,s*.098,.084},{.018,.056,.069,s*.098,.084},{.027,.053,.065,s*.098,.084}},L,dark,26,2);for(int t=0;t<6;t++){double z=.032+t*.021;toe.box({s*.098-.049,.003,z},{s*.098+.049,.009,z+.010},L,dark);}
    }
}
void GoblinRig::pose(Geometry& out,double phase,double motion,double idle,double collapse,double look,double attack,int piece,double impact) const{
    if(bones.empty()){out.points.clear();return;}motion=std::clamp(motion,0.,1.);std::vector<Vec> angles(bones.size());
    const double step=std::sin(phase),breath=std::sin(idle*2.1);angles[spine]={.025*breath+.08*motion,-.035*(1-motion),.025*step*motion-.018*(1-motion)};angles[chest]={-.012*breath,.055*step*motion,-.020*step*motion};angles[neck]={-.025*breath,.035*std::sin(idle*.8),0};angles[head]={.015*std::sin(idle*.9),.060*std::sin(idle*.7),.015*std::sin(idle*1.1)};angles[scarf]={.04*step*motion,0,.02*breath};angles[tabard]={-.12*motion+.08*step*motion,0,.035*breath};
    for(int i=0;i<2;i++){double s=i?1:-1,cycle=phase+(i?Pi:0),a=std::sin(cycle);const auto& q=sides[i];
        angles[q.hip]={.42*a*motion,0,s*.025};angles[q.knee]={-.58*std::max(0.,-a)*motion,0,0};angles[q.ankle]={-.42*a*motion+.38*std::max(0.,-a)*motion,0,0};angles[q.toe]={.14*std::max(0.,a)*motion,0,0};
        if(i==0){angles[q.shoulder]={.06*a*motion,0,-.035};angles[q.upper]={-.34*a*motion,0,.025};angles[q.elbow]={-.17*motion-.06*breath,0,0};angles[q.wrist]={.04*breath,0,.02};}
        else{angles[q.shoulder]={-.025*breath,0,.02};angles[q.upper]={.08*motion,0,0};angles[q.elbow]={-.07*motion,0,0};angles[q.wrist]={-.01*motion,0,0};}
        for(int d=0;d<5;d++)for(int j=0;j<3;j++)angles[q.digit[d][j]]={i==1?(.015+.025*j):(.07+.035*std::sin(idle*1.7+d)+.10*motion),0,d==4?s*.10:0};
    }
    angles[head].y+=std::clamp(look,-.95,.95)*.77;angles[neck].y+=std::clamp(look,-.95,.95)*.23;
    if(role==1){const auto& l=sides[0];const auto& r=sides[1];
        angles[r.upper].x=-.15;angles[r.upper].z=-.26;angles[r.elbow].y=-.97;angles[r.wrist].y=.39;
        angles[l.upper]={-1.12,-.28,.20};angles[l.elbow]={-.46,-.76,.20};angles[l.wrist]={.10,.2,0};
        for(int d=0;d<5;d++)for(int j=0;j<3;j++)angles[l.digit[d][j]].x=.32+.12*j;
    }else if(role==2){const auto& r=sides[1];angles[r.upper].x=.14;angles[r.upper].z=.05;angles[r.elbow].x=.87;angles[r.wrist].x=.55;angles[r.wrist].z=1.10;}
    if(attack>=0){
        const double t=std::clamp(attack,0.,1.),wind=std::sin(Pi*std::min(t/.46,1.)),hit=std::sin(Pi*std::clamp((t-.30)/.70,0.,1.));
        const auto& r=sides[1];angles[chest].y+=.18*wind-.30*hit;
        if(role==0){angles[r.upper].x-=.85*hit;angles[r.elbow].x+=.28*wind-.80*hit;angles[r.wrist].x+=.85*hit;}
        else if(role==2){angles[r.upper].x+=.7*wind-1.10*hit;angles[r.upper].z-=.40*wind+.70*hit;angles[r.elbow].x-=.75*wind;angles[r.wrist].x+=.55*hit;}
        else{angles[r.wrist].x-=.11*hit;angles[chest].x-=.05*hit;}
    }
    if(collapse>0){
        const double t=std::clamp(collapse,0.,1.);
        // Independent reaction curves: recoil, delayed loss of support,
        // asymmetric reach, landing and diminishing secondary motion.
        const double shock=dread::curve(std::array<dread::Key,8>{{{0,0},{.045,.85},{.09,1},{.17,.48},{.26,.13},{.39,.22},{.56,0},{1,0}}},t);
        const double buckle=dread::curve(std::array<dread::Key,9>{{{0,0},{.10,0},{.20,.10},{.34,.42},{.48,.78},{.65,1},{.76,.90},{.89,1},{1,1}}},t);
        const double brace=dread::curve(std::array<dread::Key,9>{{{0,0},{.09,.10},{.20,.58},{.33,1},{.46,.84},{.58,.50},{.71,.14},{.86,0},{1,0}}},t);
        const double settle=dread::curve(std::array<dread::Key,9>{{{0,0},{.48,0},{.64,.36},{.70,-.68},{.76,.39},{.82,-.19},{.89,.09},{.96,0},{1,0}}},t);
        const double twist=dread::curve(std::array<dread::Key,7>{{{0,0},{.12,-.22},{.27,.52},{.44,1},{.64,.64},{.81,.15},{1,0}}},t);
        // Blend out the starting pose instead of replacing every joint on
        // the very first frame. Each finger and small secondary joint lags.
        const auto living=angles;
        const double force=impact*dread::soft(t/.12)*(1-dread::soft((t-.49)/.42));
        const double rebound=dread::impactSpring(t,.64,31.,14.);
        angles[pelvis]={.13*rebound,.20*force,-.28*force+.09*rebound};
        angles[spine]={-.15*buckle-.16*shock,.11*brace+.13*twist,.20*buckle+.08*settle};
        angles[chest]={.22*shock+.15*brace,.20*buckle-.15*twist,-.16*buckle-.14*shock};
        angles[neck]={-.36*shock-.12*brace,.22*brace,.16*buckle+.08*settle};
        angles[head]={.30*buckle+.21*shock+.31*dread::impactSpring(t,.70,44.,11.),
            -.24*buckle-.16*brace-.11*force,.19*buckle+.11*settle+.08*rebound};
        angles[scarf]={.30*brace+.18*settle,.06*twist,.16*shock};angles[tabard]={-.24*brace+.16*settle,0,.09*settle};
        for(int i=0;i<2;i++){
            const double s=i?1.:-1.;const auto& q=sides[i];
            const double lag=dread::soft((t-(i?.15:.09))/.29)*(1-dread::soft((t-.53)/.38));
            angles[q.hip]={.43*buckle+s*.34*brace,0,s*.10*buckle};
            angles[q.knee]={-.88*buckle-.26*(i?shock:brace)-.15*lag,0,0};
            angles[q.ankle]={.24*buckle+.12*settle-.10*lag,0,0};angles[q.toe]={.19*shock+.11*lag,0,0};
            angles[q.shoulder]={.09*brace,0,s*.20*shock};
            angles[q.upper]={-.95*brace+.16*buckle-.22*lag,s*.18*brace,s*(.25*buckle+.35*shock)};
            angles[q.elbow]={-.75*brace-.20*buckle-.22*lag,0,s*.09*settle};
            angles[q.wrist]={.23*buckle+.24*shock+.13*settle,0,s*.15*buckle};
            // The heavier joints strike first. A delayed, damped impulse
            // travels out to the loose joints and fabric, then dies away.
            const double contact=i?.63:.56;
            angles[q.upper].z+=s*.20*dread::impactSpring(t,contact,31.,11.);
            angles[q.elbow].x+=.43*dread::impactSpring(t,contact+.03,38.,10.);
            angles[q.wrist].z+=s*.40*dread::impactSpring(t,contact+.06,42.,12.);
            angles[q.knee].x+=.40*dread::impactSpring(t,contact+.05,32.,11.);
            angles[q.ankle].x+=.32*dread::impactSpring(t,contact+.08,39.,11.);
            for(int d=0;d<5;d++)for(int j=0;j<3;j++){
                const double curl=dread::soft((t-.04-.014*d-.013*j)/.12)*(1-dread::soft((t-.19-.011*d)/.27));
                angles[q.digit[d][j]]={.12*buckle+.38*curl-.08*brace+
                    .20*dread::impactSpring(t,contact+.08+.011*(d+j),44.,12.),0,s*.065*lag*(d-2)};
            }
        }
        const double blend=dread::soft(t/.075);
        for(std::size_t i=0;i<angles.size();i++)angles[i]=living[i]+(angles[i]-living[i])*blend;
    }
    std::vector<Frame> frames(bones.size());double floor=1e9;
    for(std::size_t i=0;i<bones.size();i++){const auto& bone=bones[i];Frame r=rotation(angles[i]);if(bone.parent<0){r.p=bone.pivot;}else{const auto& p=frames[bone.parent];r.p=p.at(bone.pivot-bones[bone.parent].pivot);r.x=p.dir(r.x);r.y=p.dir(r.y);r.z=p.dir(r.z);}frames[i]=r;}
    if(role==1 && collapse<.075){
        // Two-bone support-hand IK. The crossbow is owned by the right wrist;
        // solve the left arm against a fixed point under its fore-end.
        const auto& l=sides[0];const auto& r=sides[1];
        const Vec support=frames[r.wrist].at({.008,-.064,.18});
        const Vec shoulder=frames[l.upper].p;
        const double a=length(bones[l.elbow].pivot-bones[l.upper].pivot),b=length(bones[l.wrist].pivot-bones[l.elbow].pivot);
        const double held=1-dread::soft(collapse/.075);
        const Vec oldTip=frames[l.wrist].p,oldJoint=frames[l.elbow].p;
        const Vec wish=oldTip+(support+Vec{-.022,-.036,0}-oldTip)*held-shoulder,axis=unit(wish);
        const double d=std::clamp(length(wish),std::abs(a-b)+.002,a+b-.002);
        const double along=(a*a+d*d-b*b)/(2*d),height=std::sqrt(std::max(0.,a*a-along*along));
        const Vec oldBend=unit(oldJoint-shoulder-axis*dot(oldJoint-shoulder,axis));
        const Vec hint=oldBend*(1-held)+unit(Vec{-.65,-1,.05})*held;Vec bend=unit(hint-axis*dot(hint,axis));if(length(bend)<.01)bend=unit(cross(axis,{0,0,1}));
        const Vec joint=shoulder+axis*along+bend*height,tip=shoulder+axis*d;
        const auto align=[&](Frame frame,Vec local,Vec desired){
            const Vec from=unit(frame.dir(local)),to=unit(desired);const double c=std::clamp(dot(from,to),-1.,1.);Vec k=cross(from,to);
            if(c<-.9999){k=unit(cross(from,std::abs(from.y)<.9?Vec{0,1,0}:Vec{1,0,0}));auto turn=[&](Vec v){return k*(2*dot(k,v))-v;};frame.x=turn(frame.x);frame.y=turn(frame.y);frame.z=turn(frame.z);}
            else {auto turn=[&](Vec v){return v+cross(k,v)+cross(k,cross(k,v))/(1+c);};frame.x=turn(frame.x);frame.y=turn(frame.y);frame.z=turn(frame.z);}
            return frame;
        };
        frames[l.upper]=align(frames[l.upper],bones[l.elbow].pivot-bones[l.upper].pivot,joint-shoulder);
        frames[l.elbow]=align(frames[l.elbow],bones[l.wrist].pivot-bones[l.elbow].pivot,tip-joint);frames[l.elbow].p=joint;
        auto& hand=frames[l.wrist];hand.p=tip;
        Vec desiredY=unit(tip-support);if(length(desiredY)<.1)desiredY={0,-1,0};
        hand.y=unit(hand.y*(1-held)+desiredY*held);
        Vec desiredX=unit(cross(hand.y,frames[r.wrist].z));if(length(desiredX)<.1)desiredX=unit(cross(hand.y,{1,0,0}));
        const Vec hintX=hand.x*(1-held)+desiredX*held;
        hand.x=unit(hintX-hand.y*dot(hintX,hand.y));if(length(hand.x)<.1)hand.x=desiredX;hand.z=unit(cross(hand.x,hand.y));
        for(const auto& digits:l.digit)for(int id:digits){const int parent=bones[id].parent;const auto& f=frames[parent];Frame q=rotation(angles[id]);q.p=f.at(bones[id].pivot-bones[parent].pivot);q.x=f.dir(q.x);q.y=f.dir(q.y);q.z=f.dir(q.z);frames[id]=q;}
    }
    for(auto& q:sides)for(int id:{q.ankle,q.toe})for(auto& v:bones[id].mesh.points)floor=std::min(floor,frames[id].at(Vec{v.x,v.y,v.z}-bones[id].pivot).y);
    out.points.clear();std::size_t count=0;for(auto& b:bones)count+=b.mesh.points.size();out.points.reserve(count);
    const auto partOf=[&](int id){
        for(int j=id;j>=0;j=bones[j].parent){if(j==head||j==neck)return 0;
            for(int i=0;i<2;i++){if(j==sides[i].shoulder)return 2+i;if(j==sides[i].hip)return 4+i;}}
        return 1;
    };
    const auto append=[&](int i,const Geometry& mesh){for(auto v:mesh.points){Vec p=frames[i].at(Vec{v.x,v.y,v.z}-bones[i].pivot);Vec n=frames[i].dir({v.nx,v.ny,v.nz});v.x=p.x;v.y=p.y-floor+.002;v.z=p.z;v.nx=n.x;v.ny=n.y;v.nz=n.z;
        if(collapse>0 && (int(v.id)==S || int(v.id)==E)){
            const float fade=float(dread::soft((collapse-.16)/.63));
            const float depth=int(v.id)==E?.58f:.13f;
            v.r*=1-depth*fade;v.g*=1-(depth+.045f)*fade;v.b*=1-depth*fade;
        }
        out.points.push_back(v);}};
    for(std::size_t i=0;i<bones.size();i++)if(piece<0 || partOf(int(i))==piece)append(int(i),bones[i].mesh);
    if(piece<0)append(sides[1].wrist,armament);
}
void goblin(Geometry& mesh,bool proxy){GoblinRig rig;rig.build(proxy);rig.pose(mesh,0,0,0);}

void chest(Geometry& mesh,bool open){
    constexpr Color wood{0.63f,0.39f,0.20f},metal{0.78f,0.66f,0.40f},gem{0.39f,1.11f,0.77f};
    const double height=0.34;
    for(double x:{-0.39,0.35}){
        mesh.box({x,0,-0.25},{x+0.045,height,0.28},Timber,wood);
        mesh.tube({x+0.019,0.13,0.29},{x+0.019,0.32,0.29},0.028,BrassTexture,metal,7);
    }
    mesh.box({-0.35,0.01,-0.24},{0.35,0.09,0.27},Timber,wood);
    mesh.box({-0.35,0.09,-0.24},{0.35,height,-0.19},Timber,wood);
    mesh.box({-0.35,0.09,0.21},{0.35,height,0.27},Timber,wood);
    for(int i=-2;i<=2;i++){
        const double x=i*0.144;
        mesh.tube({x,0.13,0.278},{x+0.041,0.27,0.278},0.018,BrassTexture,metal,6);
    }
    if(open)mesh.box({-0.41,height,-0.25},{0.41,0.85,-0.18},Timber,wood);
    else{
        for(int i=0;i<8;i++){
            const double a=Pi*i/8,b=Pi*(i+1)/8;
            const auto pos=[&](double x,double t){return Vec{x,height+0.25*std::sin(t),0.257*std::cos(t)};};
            mesh.quad(pos(-0.40,a),pos(0.40,a),pos(0.40,b),pos(-0.40,b),Timber,wood);
            for(double x:{-0.27,0.27})
                mesh.tube(pos(x,a)+Vec{0,0.016,0},pos(x,b)+Vec{0,0.016,0},0.023,BrassTexture,metal,6);
        }
        mesh.stone({0,0.36,0.28},0.075,FluxTexture,gem,5,9);
    }
}
}

namespace settlement {
void fortressArt(Geometry& g,Geometry& fireBase,Geometry& flame){
    const Color wood{.48f,.31f,.21f},iron{.48f,.48f,.43f},gold{.78f,.55f,.28f},cloth{.48f,.13f,.17f};
    const Color bone{.84f,.76f,.58f},moss{.24f,.45f,.27f};
    for(double x:{21.2,27.8}){
        g.tube({x,5,13.92},{x,8.8,13.92},.18,Timber,wood,8);
        g.tube({x,8.8,13.92},{24.5,9.65,13.92},.12,Timber,wood,8);
        const double s=x<24?-1:1;
        for(int i=0;i<6;i++){
            double a=x+s*(.13+i*.13),b=a+s*.13;
            auto p=[&](double q,double y){return Vec{q,y,13.84-.07*std::sin((q-x)*7)};};
            g.quad(p(a,8.15),p(b,8.15),p(b,6.28+.2*std::abs(b-x)),p(a,6.28+.2*std::abs(a-x)),GoblinClothTexture,cloth);
            g.quad(p(a,6.28+.2*std::abs(a-x)),p(b,6.28+.2*std::abs(b-x)),p(b,8.15),p(a,8.15),GoblinClothTexture,cloth);
        }
        for(double y:{5.35,6.1,7.1,8.1})g.stone({x,y,13.72},.095,GoblinBronzeTexture,gold,3,6);
    }
    for(double x:{10.5,37.5})for(double z:{16.5,41.5}){
        const double sx=x<24?-1:1,sz=z<28?-1:1;
        for(double dx:{-1.42,1.42})for(double dz:{-1.42,1.42}){
            g.tube({x+dx,5.02,z+dz},{x+dx,8.05,z+dz},.095,Timber,wood,7);
            for(double y:{5.45,7.45})g.box({x+dx-.13,y,z+dz-.13},{x+dx+.13,y+.12,z+dz+.13},GoblinBronzeTexture,gold);
        }
        for(int n=-1;n<=1;n++){
            g.tube({x+sx*1.48,8.55,z+n},{x+sx*1.85,9.8,z+n},.09,Timber,wood,7);
            g.tube({x+n,8.55,z+sz*1.48},{x+n,9.8,z+sz*1.85},.09,Timber,wood,7);
        }
    }
    for(int x=9;x<39;x++)for(int z:{14,44}){
        if(z==14 && x>=22 && x<=26)continue;
        const double face=z==14?13.96:45.04;
        g.box({double(x)+.08,6.08,face-.045},{x+.92,6.22,face+.045},Stone,iron);
    }
    for(int z=15;z<44;z++)for(int x:{8,39}){
        const double face=x==8?7.96:40.04;
        g.box({face-.045,6.08,z+.08},{face+.045,6.22,z+.92},Stone,iron);
    }
    for(Vec p:Defenses){
        g.box(p+Vec{-.41,-.27,-.43},p+Vec{.41,-.09,.43},GoblinBronzeTexture,gold);
        g.tube(p+Vec{0,-.21,0},p+Vec{0,.32,0},.16,IronOre,iron,8);
        g.box(p+Vec{-.15,.13,-.66},p+Vec{.15,.33,.55},Timber,wood);
        for(double s:{-1.,1.}){
            g.tube(p+Vec{0,.21,.36},p+Vec{s*.65,.27,.06},.065,GoblinBronzeTexture,gold,8);
            g.tube(p+Vec{s*.65,.27,.06},p+Vec{0,.25,-.52},.018,ClothTexture,bone,5);
        }
    }
    for(int x=19;x<=29;x++){
        const double y=10+(5-std::abs(x-24))/2;
        for(int z=34;z<=41;z++)for(int n=0;n<3;n++){
            const double a=x+n/3.;const double h=y+.035+.025*((x+z+n)%3);
            g.box({a+.025,h,z+.025},{a+.32,h+.07,z+.98},RoofingTexture,{.59f,.34f,.28f});
        }
    }
    g.tube({24.5,12.13,33.7},{24.5,12.13,42.3},.15,Timber,wood,9);
    for(double x:{19.2,29.8})for(double z:{34.05,41.95})g.tube({x,5,z},{x,9.1,z},.16,Timber,wood,9);
    for(double s:{-1.,1.}){
        const double x=24.5+s*1.55;g.tube({x,5,33.85},{x,7.55,33.85},.13,Timber,wood,9);
        Vec prior{x,7.3,33.72};
        for(int i=1;i<=8;i++){double t=i/8.;Vec next{x-s*.85*t,7.3+1.2*t,33.72-.22*t};g.tube(prior,next,.11*(1-.70*t),WitnessBoneTexture,bone,7);prior=next;}
    }
    g.box({23.25,5.01,37.25},{25.75,5.28,39.75},Stone,iron);
    g.box({23.45,5.28,37.45},{25.55,5.40,39.55},GoblinBronzeTexture,gold);
    for(int i=0;i<12;i++){
        const double a=2*Pi*i/12;Vec prior=BroodCradle+Vec{1.05*std::cos(a),0,1.05*std::sin(a)};
        for(int n=1;n<=9;n++){
            double t=n/9.;Vec next=BroodCradle+Vec{(1.05-.32*t)*std::cos(a),2.55*t,(1.05-.32*t)*std::sin(a)};
            g.tube(prior,next,.042+.024*std::sin(t*Pi),WitnessBoneTexture,bone,7);prior=next;
        }
        g.stone(BroodCradle+Vec{1.05*std::cos(a),.09,1.05*std::sin(a)},.095,FluxTexture,moss,4,7);
    }
    for(int i=0;i<14;i++){
        const double a=2*Pi*i/14;fireBase.stone({.70*std::cos(a),.11,.70*std::sin(a)},.17+.025*(i%3),Stone,iron,5,9);
    }
    for(int i=0;i<6;i++){
        const double a=i*Pi/3;fireBase.tube({-.51*std::cos(a),.15,-.51*std::sin(a)},{.51*std::cos(a),.28,.51*std::sin(a)},.105,Timber,wood,8);
    }
    for(int i=0;i<9;i++){
        const double a=i*2.399963,rad=.11+.10*(i%3),height=.67+.18*(i%4);
        const Vec p{rad*std::cos(a),.26,rad*std::sin(a)},tip=p+Vec{.12*std::sin(a),height,.10*std::cos(a)};
        for(int j=0;j<6;j++){
            const double b=2*Pi*j/6,c=2*Pi*(j+1)/6;
            flame.triangle(p+Vec{.16*std::cos(b),0,.16*std::sin(b)},p+Vec{.16*std::cos(c),0,.16*std::sin(c)},tip,Lamp,{1.24f,.48f+.045f*(i%3),.10f});
        }
    }
}
}
