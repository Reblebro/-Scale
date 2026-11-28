#include "atmosphere.hpp"
#include "north-art.hpp"
#include "platform.hpp"
#include "world.hpp"
#include "witness-horror.hpp"

#include <cmath>
#include <cstdint>
#include <vector>
#include <fstream>

namespace {
void queueHorrorClip(int slot,std::vector<float> clip,int channels);
}

void northSound(){
    constexpr int rate=24000;
    static const auto clip=[]{
        std::vector<float> signal(rate*5);
        std::uint32_t seed=42097;
        double noise=0,phase=0;
        for(int i=0;i<int(signal.size());i++){
            const double t=double(i)/rate;
            seed=1664525u*seed+1013904223u;
            noise=0.97*noise+0.03*(double(seed>>16)/32768-1);
            phase+=2*Pi*(53-4*std::sin(t*1.1)+2*std::sin(t*7))/rate;
            const double swell=std::min(t/0.6,1.0)*std::min((5-t)/1.0,1.0);
            const double breath=0.38+0.62*std::pow(0.5+0.5*std::cos(t*3.1),2);
            signal[i]=float(swell*breath*(0.073*std::sin(phase)+0.032*std::sin(phase*2.019)+
                0.012*std::sin(phase*4.05)+0.22*noise));
        }
        return signal;
    }();
    queueHorrorClip(0,clip,1);
}

void northGeometry(Geometry& shape,bool proxy){
    northart::Rig rig;rig.build(proxy);rig.pose(shape,0);
}

namespace {
// A rigid cranium and a hanging, narrow mandible. Broad anatomical planes
// define the silhouette; fine detail is pigment, not waves in the skull.
constexpr double SkullBottom=-.207,SkullTop=.385;
constexpr double SkullMid=(SkullTop+SkullBottom)*.5,SkullHalf=(SkullTop-SkullBottom)*.5;
struct Section {double y,width,front,back;};
constexpr std::array<Section,12> skullSections={{{SkullBottom,0,.164,.164},
    {-.199,.071,.220,.057},{-.180,.093,.242,-.045},
    {-.145,.110,.250,-.105},{-.102,.151,.233,-.177},
    {-.040,.200,.226,-.215},{.050,.211,.241,-.241},
    {.130,.196,.221,-.214},{.205,.202,.204,-.200},
    {.277,.181,.171,-.166},{.334,.128,.113,-.109},
    {SkullTop,0,-.034,-.034}}};
double ease(double a,double b,double x){double t=std::clamp((x-a)/(b-a),0.0,1.0);return t*t*(3-2*t);}
double profile(double y,int field){
    const auto value=[&](const Section& q){return field==0?q.width:field==1?q.front:q.back;};
    // Shape-preserving cubic interpolation, with no oscillating profile.
    for(std::size_t i=1;i<skullSections.size();++i){
        if(y>skullSections[i].y)continue;
        const auto& a=skullSections[i-1];const auto& b=skullSections[i];
        const double t=std::clamp((y-a.y)/(b.y-a.y),0.0,1.0),dy=b.y-a.y;
        const double slope=(value(b)-value(a))/dy;
        const auto tangent=[&](std::size_t k){
            if(k==0 || k+1==skullSections.size())return slope;
            const auto& l=skullSections[k-1];const auto& c=skullSections[k];const auto& r=skullSections[k+1];
            const double p=(value(c)-value(l))/(c.y-l.y),q=(value(r)-value(c))/(r.y-c.y);
            return p*q<=0?0.0:2*p*q/(p+q);
        };
        return (2*t*t*t-3*t*t+1)*value(a)+(t*t*t-2*t*t+t)*dy*tangent(i-1)
             +(-2*t*t*t+3*t*t)*value(b)+(t*t*t-t*t)*dy*tangent(i);
    }
    return value(skullSections.back());
}
Vec eyeCenter(int side){return {side*(side<0?.101:.105),side<0?.104:.088,0};}
double mound(double x,double y,double cx,double cy,double rx,double ry){
    const double a=(x-cx)/rx,b=(y-cy)/ry;return std::exp(-2*(a*a+b*b));
}
// Rotated, angular orbits with a sharply defined inner wall and a broad rim.
// The asymmetry is anatomical (different brow/cheek levels), not turbulence.
double orbitRadius(double x,double y,int side){
    const Vec c=eyeCenter(side);const double u=x-c.x,v=y-c.y;
    const double angle=side<0?-.21:.32;
    const double a=(u*std::cos(angle)+v*std::sin(angle))/(side<0?.061:.057);
    const double b=(-u*std::sin(angle)+v*std::cos(angle))/(side<0?.050:.047);
    // Outer corners sag but the inner brow crowds the orbit. This produces
    // an asymmetric, hooded stare rather than two round mask holes.
    const double hood=b+.25*std::max(0.0,-a*side);
    const double angleOnRim=std::atan2(hood,a);
    const double chipped=(side<0?.075:.045)*std::cos(angleOnRim*3.0+.9*side)
        +.035*std::sin(angleOnRim*7+side);
    return std::pow(std::pow(std::abs(a),2.25)+std::pow(std::abs(hood),2.35),1/2.3)+chipped;
}
double noseRadius(double x,double y){
    const double v=(y+.057)/.057;
    const double width=.0085+.006*ease(-.78,.60,-v);
    const double separation=.011+.010*ease(-.85,.60,-v);
    const double center=.009+.09*(y+.044);
    const double left=(x-center+separation)/(width*1.06);
    const double right=(x-center-separation)/(width*.90);
    const auto shape=[&](double u,double h){return std::pow(std::pow(std::abs(u),2.4)+
        std::pow(std::abs(h),2.2),1/2.3);};
    return std::min(shape(left,v),shape(right,(y+.062)/.051));
}
double recess(double r){return 1-ease(.65,1.03,r);}
// A broad, rolled orbital wall rather than a straight-sided drill hole.
// Keep its center recessed enough to seat the eye sphere.
double orbitalBowl(double r){return 1-ease(.34,1.18,r);}
double rim(double r){return ease(.77,1.03,r)*(1-ease(1.03,1.48,r));}
// Anatomical suture: narrow cut in the bone with a branching shape.
double seamDistance(Vec p,Vec a,Vec b){
    const Vec d=b-a;return length(p-(a+d*std::clamp(dot(p-a,d)/dot(d,d),0.0,1.0)));
}
double cranialSeam(double x,double y){
    constexpr std::array<Vec,8> suture={{{-.028,.381,0},{-.037,.351,0},{-.018,.327,0},
        {-.035,.308,0},{-.020,.281,0},{-.048,.260,0},{-.041,.235,0},{-.068,.219,0}}};
    const Vec q{x,y,0};double dist=1;
    for(std::size_t i=1;i<suture.size();++i)dist=std::min(dist,seamDistance(q,suture[i-1],suture[i]));
    dist=std::min(dist,seamDistance(q,suture[3],{.015,.282,0}));
    return std::min(dist,seamDistance(q,{.015,.282,0},{.043,.292,0}));
}
// Broad angular bone plates. A narrow recessed seam follows each plate
// boundary; interior height is a small fixed slope, not a ripple/noise field.
struct CrustChip {double relief,edge;};
CrustChip skullChips(double x,double y){
    const double xx=x*15.0,yy=y*15.0;const int ix=int(std::floor(xx)),iy=int(std::floor(yy));
    double first=1e9,second=1e9;Vec nearest{};std::uint32_t chosen=0;
    for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx){
        const int gx=ix+dx,gy=iy+dz;
        const auto k=mix(std::uint32_t(gx)*0x9e3779b9u^std::uint32_t(gy)*0x85ebca6bu^0x147ad3u);
        const Vec q{gx+.17+.66*double(k&255)/255.,gy+.17+.66*double((k>>8)&255)/255.,0};
        const double d=std::hypot(xx-q.x,yy-q.y);
        if(d<first){second=first;first=d;nearest=q;chosen=k;}
        else second=std::min(second,d);
    }
    const double edge=1-ease(.028,.14,second-first);
    const double plane=.009*(xx-nearest.x)*(double((chosen>>16)&15)/7.5-1.)+
        .009*(yy-nearest.y)*(double((chosen>>20)&15)/7.5-1.);
    const double height=.015*(double((chosen>>24)&255)/255.-.5);
    // Attenuate at cell borders to keep this relief continuous.
    return {(height+plane)*(1-edge)-.0065*edge,edge};
}
double frontDepth(double x,double y,double z,double facing){
    const double l=orbitRadius(x,y,-1),r=orbitRadius(x,y,1),nose=noseRadius(x,y);
    // Swept supraorbital arches, with a heavy medial brow and a thinner
    // lateral rim. The forehead no longer overhangs like a thick shelf.
    const double brow=.031*mound(x,y,-.096,.157-.28*(x+.101),.089,.026)+
                      .030*mound(x,y,.101,.143+.30*(x-.105),.086,.024)
        +.028*mound(x,y,-.039,.147,.029,.037)+.024*mound(x,y,.044,.137,.029,.035);
    const double zygoma=.038*mound(x,y,-.158,.012+.32*(x+.158),.060,.021)+
        .035*mound(x,y,.155,-.006-.32*(x-.155),.060,.021);
    const double bridge=.036*mound(x,y,.005,.057,.025,.099);
    const double temple=.064*(mound(x,y,-.198,.122,.047,.13)+mound(x,y,.198,.100,.050,.12));
    const double cheek=.090*(mound(x,y,-.140,-.110,.053,.067)+mound(x,y,.137,-.124,.049,.069));
    const double septum=.026*mound(x,y,.009+.09*(y+.044),-.064,.010,.046);
    const double orbitChip=.035*mound(x,y,-.162,.166,.018,.016);
    // Flattened frontal plates separated by weathered seams. Broad angular
    // relief rather than the rounded, balloon-like shape of the old head.
    const double frontal=.027*mound(x,y,-.081,.269,.096,.086)+.023*mound(x,y,.083,.283,.079,.082);
    const double chips=(y>.205 && y<.40 && facing>.12)?skullChips(x,y).relief*ease(.205,.26,y)*(1-ease(.35,.40,y)):0.;
    const double fossa=.041*(mound(x,y,-.126,-.085,.059,.041)+mound(x,y,.134,-.092,.051,.040));
    const double erosion=fossa+.012*mound(x,y,-.115,.308,.040,.045)+.009*mound(x,y,.11,.248,.035,.045);
    const double suture=y>.20?-.005*(1-ease(.001,.005,cranialSeam(x,y))):0;
    const double alveolar=.018*mound(x,y,-.005,-.178,.108,.015)
        *(.77+.23*std::cos(x*340));
    // Lateral orbital buttresses protect the globe in profile. Keep the
    // added bone outside the bowl; it blends into the cheek rather than
    // forming a separate ring or a pair of projecting eyeballs.
    const double outerBar=.048*mound(x,y,-.154,.101,.025,.053)+
                          .045*mound(x,y,.157,.086,.023,.050);
    const double infra=.023*(mound(x,y,-.100,.048,.067,.018)+mound(x,y,.106,.033,.062,.018));
    // Three restrained maxillary ridges converge toward the dental arch.
    const double maxilla=.018*(mound(x,y,-.065-.22*(y+.09),-.105,.015,.078)+
        mound(x,y,.060+.24*(y+.09),-.109,.016,.073))
        -.022*(mound(x,y,-.096,-.114,.024,.051)+mound(x,y,.092,-.114,.022,.053));
    return z+ease(.12,.78,facing)*(brow+zygoma+bridge-temple-cheek+suture-orbitChip+alveolar+frontal-erosion+chips
        +.010*(rim(l)+rim(r))+septum+infra+maxilla+outerBar
        -.078*orbitalBowl(l)-.073*orbitalBowl(r)-.142*recess(nose));
}
Vec skullPoint(double latitude,double longitude){
    const double y=SkullMid+SkullHalf*std::sin(latitude);
    const double w=std::max(0.0,profile(y,0));
    const double f=profile(y,1),b=profile(y,2),c=std::cos(longitude);
    const double cranialShift=.010*(y/SkullTop)*std::abs(y/SkullTop)+.014*ease(.18,.36,y);
    const double x=w*std::sin(longitude)*(1.+.04*std::sin(longitude)*ease(.18,.34,y))+cranialShift;
    // Small low-frequency cranium asymmetry, zero at the poles and away
    // from the eye and tooth geometry; not a displacement noise field.
    // Flatten the face slightly across the orbital plate; the back remains
    // a rounded cranium. No high-amplitude procedural displacement.
    const double z=(f+b)*.5+(f-b)*.5*(c>=0?std::pow(c,.71):c);
    // Low-frequency cranial plates and a flattened frontal table. The
    // faceting is limited to the cranial vault; it cannot distort the orbits.
    const double vault=ease(.20,.31,y)*std::pow(std::cos(latitude),2);
    const double plate=vault*(.012*std::cos(longitude*4+.4)+.006*std::cos(longitude*7-1.2));
    // The upper left table has collapsed towards the orbit. Its sheared rim
    // breaks the vault outline even in profile, while the socket stays intact.
    const double crown=ease(.18,.33,y);
    const double notch=crown*mound(x,y,-.105,.321,.083,.077);
    return {x+.017*crown-.012*notch,y-.032*notch-.012*crown*std::sin(longitude)*std::cos(latitude),
            frontDepth(x,y,z+plate-.031*notch,c)};
}
Vec frontPoint(double x,double y){
    const double lat=std::asin(std::clamp((y-SkullMid)/SkullHalf,-.999,.999));
    double lo=-Pi*.5,hi=Pi*.5;
    for(int i=0;i<26;++i){const double mid=(lo+hi)*.5;
        if(skullPoint(lat,mid).x<x)lo=mid;else hi=mid;}
    return skullPoint(lat,(lo+hi)*.5);
}
Color skullColor(Vec p,double facing){
    const double l=orbitRadius(p.x,p.y,-1),r=orbitRadius(p.x,p.y,1);
    const double nose=noseRadius(p.x,p.y);
    const double septum=mound(p.x,p.y,.009+.09*(p.y+.044),-.064,.010,.046);
    // A dark interior with readable, ochre rim planes. Concentrate the
    // deepest pigment behind the eyeball, not across the whole face.
    const double cavity=std::max({1-ease(.30,.76,l),1-ease(.30,.76,r),
        recess(nose)*(1-.80*septum)})*ease(.20,.78,facing);
    const double stain=std::max({1-ease(.72,1.20,l),1-ease(.72,1.20,r),
        .56*(1-ease(.90,1.34,nose))})*ease(.20,.78,facing);
    const double side=.53+.47*ease(-.6,.8,facing);
    const double mottling=.95+.05*std::sin(p.x*29+p.z*21)*std::sin(p.y*27-p.z*14);
    const double erosion=.24*mound(p.x,p.y,-.19,.045,.05,.12)+.18*mound(p.x,p.y,.16,-.14,.06,.11)
        +.12*mound(p.x,p.y,.08,.32,.10,.065);
    const double crack=(1-ease(.001,.004,cranialSeam(p.x,p.y)))*ease(.35,.8,facing);
    const double hollow=.37*(mound(p.x,p.y,-.154,-.097,.052,.053)+mound(p.x,p.y,.153,-.115,.045,.048));
    const double plateShade=(p.y>.205 && p.y<.40 && facing>.3)?skullChips(p.x,p.y).edge*ease(.205,.26,p.y)*(1-ease(.35,.40,p.y))*ease(.3,.9,facing):0.;
    const double bone=side*mottling*(1-.52*stain)*(1-.61*crack)*(1-erosion)*(1-hollow)*(1-.37*plateShade);
    const double fill=ease(.12,.90,cavity);
    return {float(std::lerp(.68*bone,.016,fill)),float(std::lerp(.64*bone,.013,fill)),
            float(std::lerp(.57*bone,.010,fill))};
}
// Actual flattened, tapered crowns, not shiny round pegs. Each tooth has a
// different height, tilt and tint. Back surfaces stay inside the oral recess.
void tooth(Geometry& shape,Vec root,Vec tip,double width,double depth,Color color){
    const Vec axis=unit(tip-root),side=unit(cross(axis,{0,0,1})),front=unit(cross(side,axis));
    std::array<Vec,8> a{},b{},c{};
    for(int i=0;i<8;i++){
        const double angle=2*Pi*i/8;
        const Vec d=side*(std::cos(angle)*width)+front*(std::sin(angle)*depth);
        a[i]=root+d*.82;b[i]=root+(tip-root)*.58+d;
        c[i]=tip+d*.68-axis*(.002*(i%3));
    }
    for(int i=0;i<8;i++){
        const int j=(i+1)%8;
        shape.quad(a[i],b[i],b[j],a[j],WitnessToothTexture,color);
        shape.quad(b[i],c[i],c[j],b[j],WitnessToothTexture,color);
        shape.triangle(c[i],tip,c[j],WitnessToothTexture,color);
        shape.triangle(root,a[i],a[j],WitnessToothTexture,color);
    }
}
struct JawNode {Vec p;double width,depth;};
// Hinges attach inside the temples. The jaw branches descend and turn into
// the narrow dental arch. The whole gap is empty, with no face shell behind
// the teeth. A dark throat is recessed well behind the dental plane.
constexpr std::array<JawNode,11> mandible={{{{-.169,-.047,-.075},.028,.030},
    {{-.162,-.132,-.040},.019,.059},{{-.139,-.221,.014},.024,.055},
    {{-.092,-.276,.100},.026,.033},{{-.046,-.318,.170},.019,.027},
    {{-.013,-.333,.202},.023,.029},{{.029,-.325,.183},.021,.028},
    {{.083,-.269,.122},.024,.033},{{.137,-.210,.034},.020,.057},
    {{.161,-.120,-.041},.019,.052},{{.167,-.042,-.069},.028,.028}}};
Vec jawCurve(int i,double t){
    const Vec a=mandible[std::max(0,i-1)].p,b=mandible[i].p,c=mandible[i+1].p,
        d=mandible[std::min(int(mandible.size())-1,i+2)].p;
    return (b*2+(c-a)*t+(a*2-b*5+c*4-d)*(t*t)+(b*3-a-c*3+d)*(t*t*t))*.5;
}
void jawGeometry(Geometry& out){
    constexpr int sides=24,steps=8,rings=(int(mandible.size())-1)*steps+1;
    struct Sample{Vec p,n;Color c;};
    std::array<std::array<Sample,sides>,rings> grid{};
    for(int j=0;j<rings;++j){
        const int i=std::min(j/steps,int(mandible.size())-2);
        const double t=double(j-i*steps)/steps;
        const Vec center=jawCurve(i,t);
        const Vec tangent=unit(jawCurve(i,t+.001)-jawCurve(i,t-.001));
        // Front/back reference remains stable through vertical and horizontal
        // jaw sections. The vertical thickness forms a broad bone plate.
        const Vec side=unit(cross(tangent,{0,0,1})),front=unit(cross(side,tangent));
        const double width=std::lerp(mandible[i].width,mandible[i+1].width,t);
        const double depth=std::lerp(mandible[i].depth,mandible[i+1].depth,t);
        for(int k=0;k<sides;++k){
            const double a=2*Pi*k/sides,cs=std::cos(a),sn=std::sin(a);
            // A slightly squared section models ramus plates instead of a
            // circular tube. Use the analytical superellipse normal.
            const auto signedPower=[](double v,double power){return std::copysign(std::pow(std::abs(v),power),v);};
            const Vec p=center+side*(width*signedPower(cs,.78))+front*(depth*signedPower(sn,.78));
            const Vec n=unit(side*(signedPower(cs,1.22)/width)+front*(signedPower(sn,1.22)/depth));
            const double color=.78+.22*std::max(0.0,n.z);
            grid[j][k]={p,n,{float(.64*color),float(.60*color),float(.52*color)}};
        }
    }
    const auto tri=[&](Sample a,Sample b,Sample c){
        const Vec n=cross(b.p-a.p,c.p-a.p);if(length(n)<1e-11)return;
        if(dot(n,a.n+b.n+c.n)<0)std::swap(b,c);
        for(const auto& v:{a,b,c})out.points.push_back({float(v.p.x),float(v.p.y),float(v.p.z),
            float(v.n.x),float(v.n.y),float(v.n.z),float(v.p.x+v.p.z*.34),float(v.p.y),
            float(WitnessBoneTexture),v.c.r,v.c.g,v.c.b});
    };
    for(int j=0;j<rings-1;++j)for(int k=0;k<sides;++k){
        const int l=(k+1)%sides;
        tri(grid[j][k],grid[j][l],grid[j+1][l]);
        tri(grid[j][k],grid[j+1][l],grid[j+1][k]);
    }
    // Ends are buried in the cranium; caps keep the mandible itself closed.
    for(int end:{0,rings-1})for(int k=0;k<sides;++k){
        const auto a=grid[end][k],b=grid[end][(k+1)%sides];
        const Vec c=end==0?mandible.front().p:mandible.back().p;
        out.triangle(c,end==0?a.p:b.p,end==0?b.p:a.p,WitnessBoneTexture,{.40f,.36f,.30f});
    }
    // Recessed black throat: never an opaque surface stretched across the
    // opening. The lower jaw and both side gaps remain visibly separate.
    // The interior is recessed behind the teeth; an irregular desiccated
    // throat and narrow connective ligaments stop the body showing through
    // frontally without filling the real side openings of the jaw.
    out.tube({-.016,-.112,-.038},{-.012,-.282,.092},.073,WitnessSkinTexture,{.024f,.019f,.018f},20);
    for(int side:{-1,1}){
        const double offset=side<0?-.018:0.0;
        const Vec a{side*.090,-.107,.084},b{side*.106,-.202+offset,.067},c{side*.074,-.278+offset,.139};
        out.tube(a,b,.009,WitnessSkinTexture,{.095f,.078f,.065f},10);
        out.tube(b,c,.007,WitnessSkinTexture,{.073f,.060f,.052f},10);
        out.tube(a+Vec{side*.022,0,-.021},c+Vec{side*.016,.015,-.011},.006,WitnessSkinTexture,{.12f,.096f,.078f},8);
    }
}

}
Vec witnessSocket(int side){
    // Head geometry is immutable: avoid repeating the inverse surface
    // solve every frame, for both rendering and hit testing.
    static const std::array<Vec,2> sockets=[] {
        std::array<Vec,2> result{};
        for(int i=0;i<2;++i){const Vec c=eyeCenter(i?1:-1);
            // Seat the spherical eye in the bowl. Only its front cap should
            // be exposed: the globe must not sit on top of the orbital rim.
            result[i]=frontPoint(c.x,c.y)+Vec{0,0,i?.008:.012};}
        return result;
    }();
    return sockets[side<0?0:1];
}
void witnessHead(Geometry& shape,Geometry* movingJaw){
    shape.points.clear();
    if(movingJaw)movingJaw->points.clear();
    Geometry& jaw=movingJaw?*movingJaw:shape;
    constexpr int rings=128,sides=160;
    struct Point{Vec p,n;Color c;};
    const auto sample=[](int y,int x){
        const double a=-Pi*.5+Pi*y/rings,b=2*Pi*x/sides;
        const Vec p=skullPoint(a,b);
        const Vec dy=skullPoint(a+.00004,b)-skullPoint(a-.00004,b);
        const Vec dx=skullPoint(a,b+.00004)-skullPoint(a,b-.00004);
        const Vec normal=(y==0 || y==rings)?Vec{0,y==0?-1.0:1.0,0}:unit(cross(dx,dy)*1e8);
        return Point{p,normal,skullColor(p,std::cos(b))};
    };
    std::vector<Point> grid((rings+1)*sides);
    for(int y=0;y<=rings;y++)for(int x=0;x<sides;x++)grid[y*sides+x]=sample(y,x);
    const auto vertex=[&](int y,int x){return grid[y*sides+x%sides];};
    const auto triangle=[&](Point a,Point b,Point c){
        if(length(cross(b.p-a.p,c.p-a.p))<1e-10)return;
        if(dot(cross(b.p-a.p,c.p-a.p),a.n+b.n+c.n)<0)std::swap(b,c);
        for(const auto& v:{a,b,c})shape.points.push_back({float(v.p.x),float(v.p.y),float(v.p.z),
            float(v.n.x),float(v.n.y),float(v.n.z),float(v.p.x+v.p.z*.34),float(v.p.y),
            float(WitnessBoneTexture),v.c.r,v.c.g,v.c.b});
    };
    for(int y=0;y<rings;y++)for(int x=0;x<sides;x++){
        const auto a=vertex(y,x),b=vertex(y,x+1),c=vertex(y+1,x+1),d=vertex(y+1,x);
        triangle(a,b,c);triangle(a,c,d);
    }
    jawGeometry(jaw);
    // A remnant of dark, desiccated fascia clings to one temple and trails
    // down the zygomatic arch. This is a thin, conforming mesh, not a noisy
    // distortion of the bone. Ragged edges expose the underlying cranium.
    constexpr int strips=40,across=12;
    struct Fascia {Vec p,n;Color c;};
    std::array<std::array<Fascia,across+1>,strips+1> dry{};
    for(int j=0;j<=strips;++j){
        const double t=double(j)/strips;
        const double y=.270-.373*t;
        const double lat=std::asin(std::clamp((y-SkullMid)/SkullHalf,-.999,.999));
        const double middle=1.68-.13*t+.075*std::sin(t*7.0);
        const double width=.13*std::pow(std::sin(Pi*t),.80)+.006;
        for(int i=0;i<=across;++i){
            const double u=2.0*i/across-1;
            const double ragged=.017*std::sin(t*51+u*3)+.012*std::sin(t*87-u*9);
            const double lon=middle+(width+ragged)*u;
            const Vec p=skullPoint(lat,lon);
            const Vec n=unit(cross(skullPoint(lat,lon+.0001)-skullPoint(lat,lon-.0001),
                skullPoint(lat+.0001,lon)-skullPoint(lat-.0001,lon))*1e8);
            const double fold=.0014*(1+std::sin(t*33+u*9));
            const double thickness=.0019+.003*(1-u*u)+fold;
            const float tint=float(.24+.11*(1-std::abs(u))+.045*std::sin(t*17+u*5));
            dry[j][i]={p+n*thickness,n,{tint,tint*.90f,tint*.78f}};
        }
    }
    const auto fasciaTri=[&](Fascia a,Fascia b,Fascia c){
        const Vec n=cross(b.p-a.p,c.p-a.p);if(length(n)<1e-11)return;
        if(dot(n,a.n+b.n+c.n)<0)std::swap(b,c);
        for(const auto& v:{a,b,c})shape.points.push_back({float(v.p.x),float(v.p.y),float(v.p.z),
            float(v.n.x),float(v.n.y),float(v.n.z),float(v.p.x*2.4+v.p.z*.45),float(v.p.y*2.1),
            float(WitnessSkinTexture),v.c.r,v.c.g,v.c.b});
    };
    for(int j=0;j<strips;++j)for(int i=0;i<across;++i){
        fasciaTri(dry[j][i],dry[j][i+1],dry[j+1][i+1]);
        fasciaTri(dry[j][i],dry[j+1][i+1],dry[j+1][i]);
    }
    // A short upper dental arch and a visibly lower, displaced mandible.
    // Teeth emerge from the walls of the cavity, never float in front of it.
    for(int row:{-1,1})for(int i=0;i<9;i++){
        const double x=-.079+i*.0185;
        const auto key=mix(0x97a2u+unsigned(i)*173u+unsigned(row+1)*467u);
        if((row<0 && i==2) || (row>0 && i==6))continue;
        const double arc=std::abs(x)/.08;
        const double xx=row<0?x*.73-.009:x-.004;
        Vec base;
        if(row>0){
            const double y=-.181-.006*arc*arc+.006*std::sin(i*1.23);
            base=frontPoint(xx,y)+Vec{0,.006,-.004};
        }else{
            // Embed each tooth root in the upper edge of the actual jaw
            // sweep, following its displaced dental arc.
            const int segment=xx<-.013?4:5;
            double lo=0,hi=1;
            // For the arch endpoints choose the neighboring ramus segment.
            int seg=segment;
            if(xx<mandible[4].p.x)seg=3;
            if(xx>mandible[6].p.x)seg=6;
            for(int j=0;j<22;++j){const double mid=(lo+hi)*.5;
                if(jawCurve(seg,mid).x<xx)lo=mid;else hi=mid;}
            base=jawCurve(seg,(lo+hi)*.5)+Vec{0,.016,.003};
        }
        const double tall=.023+.016*double(key&15)/15.0+(row>0&&(i==1||i==7)?.012:0);
        const Vec tip=base+Vec{.003*std::sin(i*3.1),-row*tall,-.004-.008*arc};
        const float tone=.53f+.18f*float((key>>16)&15)/15.f;
        tooth(row<0?jaw:shape,base,tip,.0085-.0014*arc,.008,{tone,tone*.93f,tone*.76f});
    }
    // Shorten the long muzzle and jaw as a single sculptural transformation.
    // Teeth, skin and the mandible keep their relative positions. The orbit
    // region (including the independently rotating eyes) is not moved.
    for(Geometry* part:{&shape,movingJaw})if(part)for(auto& v:part->points){
        const double d=.025-v.y;
        if(d<=0)continue;
        const double t=std::clamp(d/.09,0.,1.);
        const double blend=t*t*(3.-2.*t);
        const double slope=1.-.13*(blend+d*(t<1.?6.*t*(1.-t)/.09:0.));
        v.y=float(v.y+.13*d*blend);
        const Vec n=unit({v.nx,v.ny/slope,v.nz});
        v.nx=float(n.x);v.ny=float(n.y);v.nz=float(n.z);
    }
}

void witnessSound(double pan,double strength){
    constexpr int rate=24000,frames=rate*3;
    static std::uint32_t sequence=0;
    const float left=float(0.70-pan*0.27),right=float(0.70+pan*0.27);
    std::vector<float> samples(frames*2);
    std::uint32_t state=247621u+ ++sequence*0x9e3779b9u;
    double breath=0,grain=0,phase=0;
    for(int i=0;i<frames;i++){
        const double t=double(i)/rate;
        state=1664525u*state+1013904223u;
        const double noise=double(state>>16)/32768-1;
        breath=0.987*breath+0.013*noise;
        grain=0.83*grain+0.17*noise;
        phase+=2*Pi*(39-3*t+5*std::sin(t*2.1)+2*std::sin(t*11.3))/rate;
        const double rise=std::clamp((t-0.08)/0.60,0.0,1.0);
        const double fade=std::max(0.0,1.0-t/3);
        const double rasp=0.23+0.77*std::pow(std::sin(t*8.7+t*t),2);
        const double pulse=std::sin(phase)+.32*std::sin(phase*2.017+.9*breath)+.18*std::sin(phase*6.03+breath*5)
            +.13*std::sin(phase*9.13+grain*8)*std::pow(.5+.5*std::sin(t*5.7),3);
        const float sample=float(strength*rise*fade*fade*(0.095*pulse+rasp*(0.28*breath+0.036*grain)));
        samples[2*i]=sample*left;samples[2*i+1]=sample*right;
    }
    queueHorrorClip(1,std::move(samples),2);
}


namespace {
constexpr Color soot{0.085f,0.094f,0.100f}, hide{0.24f,0.28f,0.30f},
                tendon{0.42f,0.46f,0.43f}, ivory{0.62f,0.67f,0.64f};

// A tapered shell with a continuous ridge, built in normalized bone space.
void taper(Geometry& out,Vec a,Vec b,double ra,double rb,double depth,int material,Color color,int sides){
    const Vec axis=unit(b-a);
    const Vec right=unit(cross(std::abs(axis.z)<0.85?Vec{0,0,1}:Vec{0,1,0},axis));
    const Vec front=cross(axis,right)*depth;
    for(int i=0;i<sides;i++){
        const double t=2*Pi*i/sides,u=2*Pi*(i+1)/sides;
        const Vec n=right*std::cos(t)+front*std::sin(t);
        const Vec next=right*std::cos(u)+front*std::sin(u);
        out.quad(a+n*ra,a+next*ra,b+next*rb,b+n*rb,material,color,4);
    }
}

struct BoneFrame {
    Vec origin,x,y,z;
    // y can be scaled along the bone. Normals need the inverse scale.
    Vec normalY;
    void append(Geometry& out,const Geometry& part) const{
        for(const auto& v:part.points){
            const auto p=origin+x*v.x+y*v.y+z*v.z;
            const auto n=unit(x*v.nx+normalY*v.ny+z*v.nz);
            auto copy=v;
            copy.x=float(p.x);copy.y=float(p.y);copy.z=float(p.z);
            copy.nx=float(n.x);copy.ny=float(n.y);copy.nz=float(n.z);
            out.points.push_back(copy);
        }
    }
};

BoneFrame along(Vec root,Vec tip,Vec lateral={1,0,0}){
    Vec axis=tip-root;
    const double square=std::max(1e-8,dot(axis,axis));
    const Vec y=axis/std::sqrt(square);
    if(std::abs(dot(y,lateral))>0.96)lateral={0,0,1};
    const Vec z=unit(cross(lateral,y)),x=cross(y,z);
    return {root,x,axis,z,axis/square};
}

// The soles and fingertips lie along the sampled face, including on a vertical
// wall. Projecting a forward vector onto it keeps the talons out of the rock.
BoneFrame contactFrame(const contact::Chain& limb){
    const Vec n=limb.normal;
    const Vec x=unit(cross(n,limb.forward)),z=cross(x,n);
    return {limb.tip-n*limb.pad,x,n,z,n};
}
}

void WitnessSkin::build(bool distant){
    proxy=distant;trunk.points.clear();neck.points.clear();
    for(auto& hand:hands)hand.points.clear();
    for(auto& side:fingers)for(auto& digit:side)for(auto& segment:digit.segments)segment.points.clear();
    for(auto& foot:feet)foot.points.clear();
    const int sides=distant?6:12;
    // Recessed thorax core with individually curved skeletal ribs and a
    // segmented spine. The ribs cast their own silhouette under lantern light.
    const Color oldBone{.47f,.44f,.37f},marrow{.30f,.29f,.26f},dried{.15f,.13f,.12f};
    taper(trunk,{0,0,0},{.018,.27,-.025},.235,.105,.65,WitnessSkinTexture,dried,sides);
    taper(trunk,{.018,.27,-.025},{-.035,.76,-.105},.076,.103,.36,WitnessSkinTexture,soot,sides);
    taper(trunk,{-.035,.76,-.105},{0,1,0},.103,.118,.46,WitnessSkinTexture,dried,sides);
    const int rings=distant?5:10;
    for(int i=0;i<rings;i++){
        const double t=double(i)/double(rings-1),y=.31+.59*t;
        const double shift=.018*std::sin(t*6.2);
        trunk.stone({shift,y,-.142-.083*t},.043+.016*t,WitnessBoneTexture,marrow,4,distant?6:10);
        if(!distant)trunk.tube({shift,y,-.17-.083*t},{shift-.012,y-.025,-.23-.083*t},.020,WitnessBoneTexture,oldBone,7);
    }
    // The mantle has slipped down one upper arm. Build the two torn
    // sections in bone space so the hems move with the reaching shoulder.
    for(int i=0;i<2;i++){
        auto& sleeve=sleeves[i];
        sleeve.points.clear();
        const int folds=distant?12:28,lengthwise=distant?4:9;
        const auto vertex=[&](int ring,int col){
            const double t=double(ring)/lengthwise,u=double(col)/folds;
            const double angle=u*2*Pi;
            const double fluting=.025*std::cos(7*angle+t*2)+.012*std::cos(11*angle-2.5*t);
            const double torn=.072*std::sin(5*angle+.5)+.035*std::cos(9*angle);
            const double end=(i?.61:.31)+torn+.045*std::cos(angle);
            const double y=std::lerp(-.045,end,t);
            const double radius=.125+.070*(1-t)*(1-t)-.016*t+fluting*.42;
            return Vec{radius*std::cos(angle),y,.89*radius*std::sin(angle)};
        };
        for(int j=0;j<lengthwise;j++)for(int k=0;k<folds;k++){
            const double phi=(k+.5)*2*Pi/folds;
            if(j==lengthwise-1 && std::cos(phi*3+.7)>.75)continue;
            const auto sample=[&](int row,int column){
                const Vec p=vertex(row,column);
                const Vec around=vertex(row,column+1)-vertex(row,column-1);
                const Vec down=vertex(std::min(lengthwise,row+1),column)-vertex(std::max(0,row-1),column);
                Vec n=unit(cross(down,around));
                if(dot(n,Vec{p.x,0,p.z})<0)n=n*-1;
                const float dye=float(.095+.026*std::cos(2*Pi*column/folds*7));
                return Vertex{float(p.x),float(p.y),float(p.z),float(n.x),float(n.y),float(n.z),
                    float(column)/folds,float(row)/lengthwise,float(WitnessMantleTexture),dye*.83f,dye*.87f,dye};
            };
            const auto a=sample(j,k),b=sample(j,k+1),c=sample(j+1,k+1),d=sample(j+1,k);
            const auto tri=[&](Vertex a,Vertex b,Vertex c){
                const Vec x{a.x,a.y,a.z},y{b.x,b.y,b.z},z{c.x,c.y,c.z};
                if(dot(cross(y-x,z-x),Vec{a.nx+b.nx+c.nx,a.ny+b.ny+c.ny,a.nz+b.nz+c.nz})<0)std::swap(b,c);
                for(const auto& v:{a,b,c})sleeve.points.push_back(v);
                for(auto v:{c,b,a}){v.nx=-v.nx;v.ny=-v.ny;v.nz=-v.nz;sleeve.points.push_back(v);}
            };
            tri(a,b,c);tri(a,c,d);
        }
    }
    const int ribs=distant?5:10;
    for(int i=0;i<ribs;i++){
        const double t=double(i)/double(ribs-1),y=.34+.58*t;
        const double width=.16+.255*std::sin((.14+.80*t)*Pi*.85);
        for(int side:{-1,1}){
            const double skew=side<0?.91:1.0;
            const double sag=(side<0?.018:0.)+.015*std::sin(i*2.7);
            const std::array<Vec,5> pts={{{side*.045,y+.045,-.175},
                {side*width*.68*skew,y+.018,-.143},
                {side*width*skew,y-.025,-.02},
                {side*width*.82*skew,y-.069-sag,.159},
                {side*.047,y-.096-sag,.219}}};
            const double radius=.020+.009*std::sin(Pi*t);
            const auto center=[&](double u){
                const double q=std::clamp(u,0.,1.)*4.;const int j=std::min(3,int(q));
                const double f=q-j;
                const Vec a=pts[std::max(0,j-1)],b=pts[j],c=pts[j+1],d=pts[std::min(4,j+2)];
                return (b*2+(c-a)*f+(a*2-b*5+c*4-d)*(f*f)+(b*3-a-c*3+d)*(f*f*f))*.5;
            };
            const int steps=distant?8:24;
            // A continuously curved, flattened rib with a distinct ridge,
            // rather than joint spheres or individual cylinders.
            for(int j=0;j<steps;++j){
                const auto point=[&](double f,int k){
                    const Vec c=center(f);
                    const Vec axis=unit(center(std::min(1.,f+.002))-center(std::max(0.,f-.002)));
                    const Vec u=unit(cross(axis,{0,1,0})),v=unit(cross(axis,u));
                    const double angle=2*Pi*k/sides,r=radius*(1.-.30*f);
                    return c+u*(r*.70*std::cos(angle))+v*(r*std::sin(angle));
                };
                for(int k=0;k<sides;++k){
                    const double f=double(j)/steps,g=double(j+1)/steps;
                    const float tint=float(.82+.18*std::sin(Pi*f));
                    trunk.quad(point(f,k),point(f,k+1),point(g,k+1),point(g,k),WitnessBoneTexture,
                        {oldBone.r*tint,oldBone.g*tint,oldBone.b*tint},4);
                }
            }
            if(!distant && i%2==0){
                const Vec a=pts[2]+Vec{0,-.012,0},b=pts[3]+Vec{side*.014,-.041,-.012};
                taper(trunk,a,b,.013,.007,.80,WitnessSkinTexture,tendon,7);
            }
        }
        trunk.stone({.014*std::sin(i*1.8),y-.086,.209},.037,WitnessBoneTexture,marrow,4,distant?6:9);
    }
    // Bony shoulder blades and a segmented spine under the stretched skin.
    for(int side:{-1,1}){
        taper(trunk,{side*.07,.63,-.16},{side*.29,.95,-.11},.045,.09,.63,WitnessSkinTexture,hide,sides);
        if(!distant)for(int i=0;i<5;i++){
            const double y=.49+i*.10;
            trunk.tube({side*.04,y,-.18},{side*.13,y+.045,-.21},.015,WitnessSkinTexture,tendon,6);
        }
    }
    for(int i=0;i<(distant?4:8);i++){
        const double y=.30+i*.074;
        trunk.stone({-.025,y,-.17-.075*y},.045,WitnessBoneTexture,marrow,4,7);
    }
    // Ragged front lapels wrap over the clavicles and down the sides of
    // the rib cage. Both edges taper inwards, leaving the central ribs and
    // sternum exposed. Their positions remain inside the torso envelope.
    for(int side:{-1,1}){
        const int strips=distant?5:13,columns=distant?3:7;
        const auto foldPoint=[&](int j,int k){
            const double f=double(j)/strips,u=double(k)/columns;
            const double offset=side<0?.09:0.;
            const double hem=.29+.09*std::sin(u*12.+side)+offset;
            const double y=std::lerp(.985-.09*u,hem,f);
            const double inner=.075+.12*std::sin(f*Pi*.65)+.025*f;
            const double outer=.295+.025*std::sin(Pi*f);
            const double ripple=.012*std::sin(u*17.+f*3.7)+.008*std::sin(u*31.-f*5.);
            return Vec{side*(std::lerp(inner,outer,u)+ripple),y,
                .13+.055*std::sin(Pi*f)+.025*std::cos(u*Pi*1.5)+ripple};
        };
        for(int j=0;j<strips;++j)for(int k=0;k<columns;++k){
            if(j==strips-1 && (k*3+side+7)%5==0)continue;
            const Vec a=foldPoint(j,k),b=foldPoint(j,k+1),c=foldPoint(j+1,k+1),d=foldPoint(j+1,k);
            const float w=float(.5+.5*std::cos(k*2.1+j*.3));
            const Color dye{std::lerp(.055f,.115f,w),std::lerp(.049f,.096f,w),std::lerp(.041f,.075f,w)};
            trunk.quad(a,b,c,d,WitnessMantleTexture,dye,3);trunk.quad(d,c,b,a,WitnessMantleTexture,dye,3);
        }
    }
    // Collar and neck bend with the solver instead of shearing the torso.
    // Anatomical, curved clavicles connect the sternum to the shoulder.
    // A swept bone with smoothly varying width instead of two capped rods.
    for(int side:{-1,1}){
        const Vec sternum{side*.028,.925,.143},curve{side*.20,1.035,.080},shoulder{side*.43,.968,-.025};
        const int steps=distant?4:12;
        const auto center=[&](double t){return sternum*((1-t)*(1-t))+curve*(2*t*(1-t))+shoulder*(t*t);};
        const auto point=[&](double t,int i){
            const Vec axis=unit(center(std::min(1.,t+.002))-center(std::max(0.,t-.002)));
            const Vec x=unit(cross(axis,{0,1,0})),z=cross(axis,x);
            const double a=2*Pi*i/sides,r=.025+.009*std::abs(2*t-1)+.003*std::sin(a*3);
            return center(t)+x*(r*.83*std::cos(a))+z*(r*std::sin(a));
        };
        for(int j=0;j<steps;++j)for(int k=0;k<sides;++k){
            const double f=double(j)/steps,g=double(j+1)/steps;
            trunk.quad(point(f,k),point(f,k+1),point(g,k+1),point(g,k),WitnessBoneTexture,oldBone,4);
        }
    }
    taper(neck,{0,0,0},{-.014,1,.010},.063,.048,.86,WitnessSkinTexture,soot,sides);
    for(int i=0;i<(distant?4:7);++i){
        const double f=double(i)/(distant?3:6),y=.10+.80*f;
        neck.stone({-.012*f,y,-.031},.047-.010*f,WitnessBoneTexture,marrow,4,distant?6:9);
    }
    for(int side:{-1,1}){
        const Vec a{side*.080,0,.039},b{side*.051,.46,.068},c{side*.040,.97,.032};
        taper(neck,a,b,.026,.018,.72,WitnessSkinTexture,tendon,sides);
        taper(neck,b,c,.018,.013,.70,WitnessSkinTexture,tendon,sides);
        taper(neck,{side*.045,0,-.052},{side*.034,.97,-.023},.021,.013,.72,WitnessSkinTexture,hide,sides);
    }

    for(int side=0;side<2;side++){
        const double sign=side?1:-1;
        auto& hand=hands[side];auto& foot=feet[side];
        // The rig tip is just outside the solid face. All toe pads and hooked
        // fingers terminate on its tangent plane, even on an inclined face.
        hand.tube({0,0.08,0},{0,0.03,0},0.045,WitnessSkinTexture,hide,sides);
        taper(hand,{0,0.048,-0.073},{0,0.018,0.036},0.067,0.083,0.44,WitnessSkinTexture,hide,sides);
        if(!distant)for(int k=0;k<4;++k){const double x=(k-1.5)*.030;
            taper(hand,{x*.62,.063,-.057},{x,.048,.040},.012,.014,.65,WitnessBoneTexture,oldBone,9);
            hand.stone({x,.048,.040},.014,WitnessBoneTexture,oldBone,4,8);
            taper(hand,{x,.052,.043},{x*1.18,.080,.118},.006,.004,.65,WitnessSkinTexture,tendon,6);
        }
        for(int f=0;f<(distant?3:5);f++){
            const double j=distant?2.0*f:f;
            const bool thumb=j==0;
            const Vec root{sign*(j-2)*0.03,0.025,thumb?-0.035:0.026};
            const Vec joint=root+Vec{sign*(j-2)*.021,.055,thumb?.082:.170};
            const Vec hook=joint+Vec{sign*(j-2)*.012,-.033,thumb?.032:.096-.015*std::abs(j-2)};
            const Vec end=hook+Vec{0,-.044,thumb?.019:.055};
            auto& digit=fingers[side][int(j)];digit.pivots={root,joint,hook};
            taper(digit.segments[0],root,joint,thumb?.023:.018,.013,.85,WitnessBoneTexture,oldBone,9);
            if(!distant)digit.segments[0].stone(joint,.016,WitnessBoneTexture,marrow,4,8);
            taper(digit.segments[1],joint,hook,.014,.009,.84,WitnessBoneTexture,oldBone,9);
            taper(digit.segments[2],hook,end,.010,.003,.75,WitnessBoneTexture,ivory,7);
        }
        foot.tube({0,0.094,0},{0,0.036,0},0.048,WitnessSkinTexture,hide,sides);
        taper(foot,{0,0.055,-0.085},{0,0.026,0.116},0.053,0.067,0.55,WitnessSkinTexture,hide,sides);
        // Actual metatarsals and articulated toe bones. Keep the same
        // forward contact reach and the original three-toed silhouette.
        for(int toe=0;toe<3;toe++){
            const double x=(toe-1)*.038;
            const Vec root{x,.031,.093},knuckle=root+Vec{x*.20,.007,.052},
                tip=knuckle+Vec{x*.12,-.025,.053-.011*std::abs(toe-1)};
            taper(foot,{x*.45,.067,-.061},root,.014,.013,.72,WitnessBoneTexture,oldBone,8);
            taper(foot,root,knuckle,.016,.012,.75,WitnessBoneTexture,oldBone,8);
            taper(foot,knuckle,tip,.012,.006,.72,WitnessBoneTexture,ivory,8);
            if(!distant)foot.stone(knuckle,.012,WitnessBoneTexture,marrow,4,8);
        }
    }
}

namespace {
// Each bone is an independent swept, closed surface with flared articular
// ends and a subtly bowed triangular shaft. The two lower bones leave a
// real interosseous gap. The existing solved joint and contact remain fixed.
void skeletalBone(Geometry& mesh,Vec start,Vec end,Vec across,double width,double endA,double endB,
                  double bow,Color color,bool distant){
    const Vec axis=unit(end-start);
    if(length(axis)<.5)return;
    Vec x=unit(across-axis*dot(across,axis));
    if(length(x)<.5)x=unit(cross(axis,std::abs(axis.y)<.85?Vec{0,1,0}:Vec{1,0,0}));
    const Vec z=cross(axis,x);
    const int rings=distant?10:24,sides=distant?8:16;
    const auto point=[&](double t,double a){
        const double flareA=std::exp(-std::pow((t-.075)/.105,2));
        const double flareB=std::exp(-std::pow((t-.925)/.105,2));
        const double r=width+(endA-width)*flareA+(endB-width)*flareB;
        const double cap=std::sqrt(std::max(0.,1-std::pow(std::max(0.,std::abs(t-.5)-.455)/.045,2)));
        const double ridge=1+.10*std::cos(3*a+.8*t)+.045*std::cos(5*a-2*t);
        return start+(end-start)*t+x*(bow*std::sin(Pi*t))+ (x*std::cos(a)+z*(.76*std::sin(a)))*(r*cap*ridge);
    };
    const auto vertex=[&](int i,int j){
        const double t=double(i)/rings,a=2*Pi*j/sides;
        const Vec p=point(t,a);
        const Vec tangent=point(std::min(1.,t+.001),a)-point(std::max(0.,t-.001),a);
        const Vec around=point(t,a+.001)-point(t,a-.001);
        Vec n=i==0?axis*-1:i==rings?axis:unit(cross(around,tangent));
        const Vec radial=x*std::cos(a)+z*std::sin(a);
        if(i>0 && i<rings && dot(n,radial)<0)n=n*-1;
        const float shade=float(.89+.11*std::cos(a*2+.7)+.055*std::sin(t*18+a));
        return Vertex{float(p.x),float(p.y),float(p.z),float(n.x),float(n.y),float(n.z),float(j)/sides,float(t)*1.6f,
            float(WitnessBoneTexture),color.r*shade,color.g*shade,color.b*shade};
    };
    // Cache each ring: this mesh follows the rig each frame.
    std::vector<Vertex> grid((rings+1)*(sides+1));
    for(int i=0;i<=rings;++i)for(int j=0;j<=sides;++j)grid[i*(sides+1)+j]=vertex(i,j);
    const auto at=[&](int i,int j){return grid[i*(sides+1)+j];};
    const auto tri=[&](Vertex a,Vertex b,Vertex c){
        const Vec p{a.x,a.y,a.z},q{b.x,b.y,b.z},r{c.x,c.y,c.z};
        const Vec n=cross(q-p,r-p);if(length(n)<1e-10)return;
        if(dot(n,Vec{a.nx+b.nx+c.nx,a.ny+b.ny+c.ny,a.nz+b.nz+c.nz})<0)std::swap(b,c);
        for(auto v:{a,b,c})mesh.points.push_back(v);
    };
    for(int i=0;i<rings;++i)for(int j=0;j<sides;++j){
        tri(at(i,j),at(i,j+1),at(i+1,j+1));tri(at(i,j),at(i+1,j+1),at(i+1,j));
    }
}
const Geometry& skeletalTemplate(bool arm,bool distant,int which){
    static const auto forms=[] {
        std::array<Geometry,12> result;
        const Color bone{.53f,.48f,.40f},secondary{.43f,.39f,.33f};
        for(int proxy=0;proxy<2;++proxy)for(int limb=0;limb<2;++limb){
            const bool a=limb==0;
            auto* g=result.data()+proxy*6+limb*3;
            skeletalBone(g[0],{},{0,1,0},{1,0,0},a?.057:.080,a?.090:.120,a?.070:.089,a?.010:.017,bone,proxy);
            skeletalBone(g[1],{},{0,1,0},{1,0,0},a?.036:.047,a?.042:.053,a?.043:.050,a?.016:.021,bone,proxy);
            skeletalBone(g[2],{},{0,1,0},{1,0,0},a?.024:.025,a?.038:.034,a?.029:.033,a?-.020:-.024,secondary,proxy);
        }
        return result;
    }();
    return forms[(distant?6:0)+(arm?0:3)+which];
}
void livingLimb(Geometry& mesh,const contact::Chain& chain,bool arm,bool distant,int side){
    const Vec a=chain.root,b=chain.joint,c=chain.tip;
    const Vec upper=unit(b-a),lower=unit(c-b);
    Vec across=unit(cross(upper,lower));
    if(length(across)<.5)across=unit(cross(lower,std::abs(lower.y)<.85?Vec{0,1,0}:Vec{1,0,0}));
    if(length(across)<.5)return;
    across=across*side;
    const Color bone{.53f,.48f,.40f};
    // Humerus/femur: broad proximal head, narrow shaft, paired condyles.
    along(a-upper*.025,b+upper*.020,across).append(mesh,skeletalTemplate(arm,distant,0));
    const double nearGap=arm?.033:.039,farGap=arm?.027:.032;
    // Distinct radius & ulna / tibia & fibula, bowed away from each other.
    // Their outer bounds stay within the existing collision envelopes.
    along(b+across*nearGap-lower*.012,c+across*farGap+lower*.012,across).append(mesh,skeletalTemplate(arm,distant,1));
    along(b-across*nearGap-lower*.020,c-across*farGap+lower*.008,across).append(mesh,skeletalTemplate(arm,distant,2));
    // Small dark connective structures, not a continuous skin sleeve.
    const Vec flex=unit(cross(across,lower));
    const double jointRadius=arm?.041:.052;
    mesh.stone(b+flex*.010,jointRadius,WitnessSkinTexture,{.20f,.17f,.13f},distant?4:6,distant?7:12);
    for(int sideOfJoint:{-1,1}){
        const Vec offset=across*(sideOfJoint*(arm?.040:.052));
        taper(mesh,b-upper*.074+offset,b+lower*.075+offset,arm?.012:.016,arm?.010:.014,.75,WitnessSkinTexture,{.22f,.18f,.14f},distant?5:8);
    }
    if(!arm)mesh.stone(b+flex*.046,.037,WitnessBoneTexture,bone,distant?4:6,distant?7:12);
    taper(mesh,c-lower*.046,c+lower*.025,arm?.038:.046,arm?.035:.041,.85,WitnessSkinTexture,{.16f,.13f,.10f},distant?6:10);
}

}

namespace {
// Smooth attachment sleeve. Joins the anatomical bone to the torso without
// the abrupt capped cylinders of the previous shoulder/hip bridges.
void attachment(Geometry& mesh,Vec root,Vec joint,double start,double finish,bool proxy){
    const Vec axis=unit(joint-root);
    const Vec x=unit(cross(std::abs(axis.y)<.88?Vec{0,1,0}:Vec{1,0,0},axis)),z=cross(axis,x);
    const int steps=proxy?4:10,sides=proxy?8:18;
    struct P{Vec p,n;Color c;float u,v;};
    const auto sample=[&](int i,int j){
        const double t=double(i)/steps,a=2*Pi*j/sides;
        const double r=std::lerp(start,finish,t)*(.88+.11*std::cos(a*2+.4)+.07*std::sin(a*3+t*2));
        const Vec n=unit(x*std::cos(a)+z*(std::sin(a)/.82));
        const Vec p=root+(joint-root)*t+(x*std::cos(a)+z*(.82*std::sin(a)))*r;
        const float pigment=float(.31+.15*t+.07*std::cos(a*2));
        return P{p,n,{pigment,pigment*.87f,pigment*.72f},float(j)/sides,float(t)};
    };
    const auto tri=[&](P a,P b,P c){
        if(length(cross(b.p-a.p,c.p-a.p))<1e-10)return;
        if(dot(cross(b.p-a.p,c.p-a.p),a.n+b.n+c.n)<0)std::swap(b,c);
        for(const auto& v:{a,b,c})mesh.points.push_back({float(v.p.x),float(v.p.y),float(v.p.z),float(v.n.x),float(v.n.y),float(v.n.z),v.u,v.v,float(WitnessBoneTexture),v.c.r,v.c.g,v.c.b});
    };
    for(int i=0;i<steps;++i)for(int j=0;j<sides;++j){
        const auto a=sample(i,j),b=sample(i,j+1),c=sample(i+1,j+1),d=sample(i+1,j);
        tri(a,b,c);tri(a,c,d);
    }
}
}
namespace {
// Cut one continuous mantle around the shoulder girdle. The top follows
// the clavicles while the hem hangs under gravity as the torso pitches.
void drapeMantle(Geometry& out,const contact::Pose& pose,bool proxy){
    const int columns=proxy?16:38,rows=proxy?5:11;
    const Vec axis=unit(pose.chest-pose.pelvis);
    const Vec across=unit(pose.arms[1].root-pose.arms[0].root);
    const Vec back=unit(Vec{axis.x,0,axis.z})*-1;
    const Vec rear=length(back)>.5?back:Vec{0,0,-1};
    const auto point=[&](int row,int col){
        const double t=double(row)/rows,u=double(col)/columns;
        const double angle=-2.28+4.56*u;
        const double side=std::sin(angle),wrap=std::cos(angle);
        const double fold=std::cos(32*u+1.8*t+.72*std::sin(8*u-2*t));
        const double pleat=(.019+.043*t)*fold+.015*std::cos(53*u-t);
        const double worn=.084*std::sin(24*u+.9)+.057*std::sin(66*u);
        const double tear=.21*std::max(0.,std::cos(26*u+.3))*std::max(0.,std::cos(8*u-.7));
        const double hood=std::max(0.,wrap*.8+.36*side);
        const Vec collar=pose.chest-axis*.04+across*(side*.48)+rear*(wrap*.27)
            +Vec{0,.105+.16*hood-.035*side,0};
        const Vec mid=pose.chest-axis*.40+across*(side*(.68+pleat))+rear*(.20+.38*wrap)
            +Vec{0,.04+.14*wrap,0};
        Vec hem=pose.pelvis+across*(side*(.63+pleat))+rear*(.30+.43*wrap);
        // The front edges fall beside the upper arms, leaving the sternum open.
        hem+=axis*(.68*(1-wrap)*.5);
        hem.y=std::max(.15,pose.pelvis.y-.65+.24*(1-wrap)+worn+tear);
        const double v=1-t;
        return collar*(v*v)+mid*(2*t*v)+hem*(t*t)
            +(rear*wrap+across*side)*(pleat*std::sin(Pi*t));
    };
    std::vector<Vec> samples((rows+1)*(columns+1));
    const auto at=[&](int j,int i)->Vec&{return samples[j*(columns+1)+i];};
    for(int j=0;j<=rows;j++)for(int i=0;i<=columns;i++)at(j,i)=point(j,i);
    for(int j=0;j<rows;j++)for(int i=0;i<columns;i++){
        const auto key=mix(unsigned(i)*1543u+871u);
        if(j==rows-1 && key%7==0)continue;
        const auto vertex=[&](int y,int x){
            const Vec p=at(y,x);
            Vec n=unit(cross(at(y,std::min(columns,x+1))-at(y,std::max(0,x-1)),
                at(std::min(rows,y+1),x)-at(std::max(0,y-1),x)));
            const float hem=float(y)/rows;
            const float tone=.10f+.055f*hem+.012f*std::cos(x*1.63f);
            return Vertex{float(p.x),float(p.y),float(p.z),float(n.x),float(n.y),float(n.z),
                float(x)/columns,float(y)/rows,float(WitnessMantleTexture),tone*.82f,tone*.86f,tone};
        };
        const auto a=vertex(j,i),b=vertex(j,i+1),c=vertex(j+1,i+1),d=vertex(j+1,i);
        for(const auto& v:{a,b,c,a,c,d})out.points.push_back(v);
        for(auto v:{c,b,a,d,c,a}){v.nx=-v.nx;v.ny=-v.ny;v.nz=-v.nz;out.points.push_back(v);}
    }
}
}
void WitnessSkin::pose(Geometry& mesh,const contact::Pose& pose) const{
    mesh.points.clear();
    along(pose.pelvis,pose.chest).append(mesh,trunk);
    drapeMantle(mesh,pose,proxy);
    along(pose.chest,pose.neck).append(mesh,neck);
    for(int i=0;i<2;i++){
        const auto& arm=pose.arms[i];const auto& leg=pose.legs[i];
        // Moving scapula/hip bridges connect to the actual solved roots, not
        // to a static shoulder position in the torso's coordinate frame.
        const double sign=i?1.0:-1.0;
        const Vec axial=unit(pose.chest-pose.pelvis);
        const Vec lateral{sign,0,0};
        const Vec shoulder=pose.chest-axial*.17+lateral*.19;
        const Vec hip=pose.pelvis+axial*.09+lateral*.07;
        attachment(mesh,shoulder,arm.root+unit(arm.joint-arm.root)*.13,.137,.102,proxy);
        attachment(mesh,hip,leg.root+unit(leg.joint-leg.root)*.13,.155,.122,proxy);
        livingLimb(mesh,arm,true,proxy,i?1:-1);
        along(arm.root,arm.joint).append(mesh,sleeves[i]);
        livingLimb(mesh,leg,false,proxy,i?1:-1);
        const auto hand=contactFrame(arm);
        hand.append(mesh,hands[i]);
        for(int f=0;f<5;f++){
            const auto& digit=fingers[i][f];
            if(digit.segments[0].points.empty())continue;
            const double grip=std::clamp(arm.grip,0.,1.);
            if(grip==0){for(const auto& segment:digit.segments)hand.append(mesh,segment);continue;}
            // Rotate each phalanx about its own pivot and inherit the
            // previous joint's transform. The palm and wrist remain fixed.
            Vec origin=digit.pivots[0];double angle=0;
            const auto rotate=[](Vec p,double a){const double c=std::cos(a),s=std::sin(a);return Vec{p.x,p.y*c-p.z*s,p.y*s+p.z*c};};
            const auto vector=[&](Vec p){return hand.x*p.x+hand.y*p.y+hand.z*p.z;};
            for(int j=0;j<3;j++){
                if(j)origin+=rotate(digit.pivots[j]-digit.pivots[j-1],angle);
                angle+=grip*(f==0?.24:(j==0?.40:j==1?.48:.30));
                const Vec y=vector(rotate({0,1,0},angle)),z=vector(rotate({0,0,1},angle));
                const Vec shift=hand.origin+vector(origin-rotate(digit.pivots[j],angle));
                BoneFrame{shift,hand.x,y,z,y}.append(mesh,digit.segments[j]);
            }
        }
        contactFrame(leg).append(mesh,feet[i]);
    }
}

void witnessEye(Geometry& eye,bool clouded){
    const double r=clouded?.041:.038;
    const int slices=64;
    constexpr std::array<double,15> rings={0.,.10,.21,.29,.35,.44,.55,.63,.72,.83,1.02,1.33,1.72,2.35,Pi};
    struct P{Vec p,n;Color c;};
    const auto sample=[&](int j,int k){
        const double theta=rings[j],phi=2*Pi*k/slices;
        const Vec n{std::sin(theta)*std::cos(phi),std::sin(theta)*std::sin(phi),std::cos(theta)};
        // The pale, cataract-like eye is almost all iris, with a tiny pupil.
        // The other is dark and glassy, not an amber target/bullseye.
        const double pupil=clouded?.065:.19;
        const float filaments=float(.5+.5*std::sin(phi*23.+std::sin(phi*7.)*2.));
        const float grain=float(.5+.5*std::sin(phi*9.+theta*29.));
        const float irisFade=float(1-ease(.70,1.21,theta));
        const float pin=float(1-ease(pupil*.6,pupil*1.7,theta));
        const float cloud=float(.82+.12*grain+.06*filaments);
        const Color pale{.29f*cloud,.32f*cloud,.25f*cloud};
        const Color dark{.065f+.015f*grain,.066f+.011f*grain,.048f+.010f*grain};
        const Color iris=clouded?pale:dark;
        const Color sclera=clouded?Color{.065f,.067f,.052f}:Color{.028f,.027f,.024f};
        const float fade=irisFade*(1-pin);
        Color c{.009f*pin+iris.r*fade+sclera.r*(1-irisFade)*(1-pin),
            .009f*pin+iris.g*fade+sclera.g*(1-irisFade)*(1-pin),
            .008f*pin+iris.b*fade+sclera.b*(1-irisFade)*(1-pin)};
        return P{n*r,n,c};
    };
    const auto triangle=[&](P a,P b,P c){
        if(length(cross(b.p-a.p,c.p-a.p))<1e-12)return;
        if(dot(cross(b.p-a.p,c.p-a.p),a.n+b.n+c.n)<0)std::swap(b,c);
        for(const auto& v:{a,b,c})eye.points.push_back({float(v.p.x),float(v.p.y),float(v.p.z),float(v.n.x),float(v.n.y),float(v.n.z),0,0,float(WitnessEyeTexture),v.c.r,v.c.g,v.c.b});
    };
    for(int j=0;j<int(rings.size())-1;++j)for(int i=0;i<slices;++i){
        const auto a=sample(j,i),b=sample(j,i+1),c=sample(j+1,i+1),d=sample(j+1,i);
        triangle(a,b,c);triangle(a,c,d);
    }
}

// Bounded stereo mixer: one short queue for woodland ambience and diegetic
// creature calls. A different device is not opened for every sound event.
namespace {
struct ForestMixer {
    static constexpr int Rate=24000,Block=512;
    Uint32 device{};
    bool unavailable{};
    std::uint32_t seed=1721357;
    std::uint64_t samples{};
    double wind{},gain{},target{},voiceTime=10,voicePan{},voiceStrength{};
    struct Clip {std::vector<float> data;std::size_t position{};int channels=1;};
    std::array<Clip,2> clips;
    int identity{};
    bool imitation{},dark{},speech{};
    std::array<std::vector<float>,4> words;
    double voiceSample() const {
        if(!speech || identity>=4 || words[identity].empty())return dread::voice(voiceTime,identity,imitation);
        const auto& wave=words[identity];
        const auto sample=[&](double t){
            const double x=t*Rate;
            if(x<0 || x>=wave.size()-1)return 0.;
            const auto i=std::size_t(x);return std::lerp(double(wave[i]),double(wave[i+1]),x-i);
        };
        if(!imitation)return .40*sample(voiceTime);
        // Same remembered words, but time and formants no longer sound quite
        // human. The secondary voice trails by a fraction of a syllable.
        const double main=sample(voiceTime*.86+.0019*std::sin(voiceTime*18));
        const double echo=sample((voiceTime-.105)*.81);
        return .44*(main*.83+echo*.21);
    }
    bool open(){
        if(device)return true;
        if(unavailable)return false;
        if(SDL_InitSubSystem(0x10)!=0){unavailable=true;return false;}
        SDL_AudioSpec spec{};spec.freq=Rate;spec.format=0x8120;spec.channels=2;spec.samples=Block;
        device=SDL_OpenAudioDevice(nullptr,0,&spec,nullptr,0);
        if(!device){unavailable=true;return false;}
        SDL_PauseAudioDevice(device,0);return true;
    }
    void fill(){
        if(!device)return;
        const unsigned bytes=Block*2*sizeof(float);
        // No backlog of old ambience when the creature enters the radius.
        for(int block=0;block<4 && SDL_GetQueuedAudioSize(device)<bytes*3;block++){
            std::array<float,Block*2> buffer{};
            for(int i=0;i<Block;i++){
                const double t=double(samples++)/Rate;
                seed=seed*1664525u+1013904223u;
                const double white=double(seed>>16)/32768.-1.;
                wind=.992*wind+.008*white;
                gain+=(target-gain)*.0009;
                const double insect=std::pow(.5+.5*std::sin(t*19.),14)*std::sin(t*2*Pi*2850);
                const double phrase=std::fmod(t,7.1);
                const double bird=phrase<.46?std::pow(std::sin(Pi*phrase/.46),3)*
                    std::sin(2*Pi*(1350*phrase+380*phrase*phrase+23*std::sin(phrase*13))):0.;
                // No artificial heartbeat or ambient tone while the forest is silent.
                const double ambience=gain*(.10*wind+(dark?.014*insect:.028*bird));
                const double call=voiceSample()*voiceStrength;
                voiceTime+=1./Rate;
                double left=ambience+call*(.7-.3*voicePan),right=ambience+call*(.7+.3*voicePan);
                for(auto& clip:clips)if(clip.position+clip.channels<=clip.data.size()){
                    left+=clip.data[clip.position];right+=clip.data[clip.position+clip.channels-1];
                    clip.position+=clip.channels;
                }
                buffer[i*2]=float(std::clamp(left,-.85,.85));
                buffer[i*2+1]=float(std::clamp(right,-.85,.85));
            }
            if(SDL_QueueAudio(device,buffer.data(),bytes)!=0)break;
        }
    }
};
ForestMixer forestMixer;
void queueHorrorClip(int slot,std::vector<float> clip,int channels){
    if(!forestMixer.open())return;
    auto& active=forestMixer.clips[slot];
    if(active.position<active.data.size())return;
    active={std::move(clip),0,channels};forestMixer.fill();
}
}
void forestAudio(bool enabled,double gain,bool night){
    if(enabled && !forestMixer.open())return;
    if(!forestMixer.device)return;
    forestMixer.target=enabled?std::clamp(gain,0.,1.):0.;forestMixer.dark=night;
    if(!enabled)forestMixer.voiceTime=10;
    forestMixer.fill();
}
void loadCreatureVoices(const std::filesystem::path& assets){
    constexpr std::array<const char*,4> names={"aurel","mira","lethan","sable"};
    for(int id=0;id<4;id++){
        std::ifstream in(assets/"voices"/(std::string(names[id])+".pcm"),std::ios::binary);
        if(!in)continue;
        auto& wave=forestMixer.words[id];wave.clear();
        for(int i=0;i<ForestMixer::Rate*8;i++){
            unsigned char bytes[2];if(!in.read(reinterpret_cast<char*>(bytes),2))break;
            const int bits=int(bytes[0])|(int(bytes[1])<<8);
            wave.push_back(float(bits>=32768?bits-65536:bits)/32768.f);
        }
    }
}
void creatureVoice(int identity,bool imitation,double pan,double strength,bool speech){
    if(!forestMixer.open())return;
    forestMixer.identity=std::clamp(identity,0,5);forestMixer.imitation=imitation;forestMixer.speech=speech;
    forestMixer.voicePan=std::clamp(pan,-1.,1.);forestMixer.voiceStrength=std::clamp(strength,0.,1.);
    forestMixer.voiceTime=0;
    forestMixer.fill();
}
