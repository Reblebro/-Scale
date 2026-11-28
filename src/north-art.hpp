#pragma once
#include "render.hpp"
#include <bit>
#include <unordered_map>

// North is a ritual automaton. Mesh sections are authored in rest space;
// each child inherits its parent's rotation and translation about a pivot.
namespace northart {
constexpr Color bone{.80f,.68f,.53f},edge{.39f,.27f,.16f},iron{.19f,.13f,.10f},
    cord{.044f,.033f,.030f},cloth{.61f,.49f,.36f},dark{.087f,.058f,.047f},
    violet{.38f,.035f,.68f},bright{.77f,.16f,1.12f};
inline double rnd(unsigned key){return double(mix(key)&65535)/65535.;}
inline Vec rot(Vec p,Vec a){
    const double cx=cos(a.x),sx=sin(a.x),cy=cos(a.y),sy=sin(a.y),cz=cos(a.z),sz=sin(a.z);
    p={p.x,p.y*cx-p.z*sx,p.y*sx+p.z*cx};
    p={p.x*cy+p.z*sy,p.y,-p.x*sy+p.z*cy};
    return {p.x*cz-p.y*sz,p.x*sz+p.y*cz,p.z};
}
struct Joint {int parent;Vec pivot;Vec motion;Geometry mesh;
    std::vector<Vertex> unique;
    std::vector<unsigned> order;
};
// Exact float-bit keys: preserve UV seams, normals, materials and triangle order.
struct VertexHash {
    std::size_t operator()(const std::array<unsigned,12>& key) const {
        std::size_t h=0;for(auto word:key)h=(h*16777619u)^word;return h;
    }
};
struct Rig {
    std::vector<Joint> joints;
    int head{},neck{},spine{},hip{};
    std::array<int,2> thighs{},shins{},feet{},shoulders{};
    std::size_t vertexCount{},uniqueCount{};
    std::vector<unsigned> indices;
    mutable std::vector<Vertex> transformed;
    bool distant{};
    int add(int parent,Vec pivot,Vec motion={}){
        joints.push_back({parent,pivot,motion,{},{},{}});return int(joints.size())-1;
    }
    Geometry& g(int i){return joints[i].mesh;}
    void build(bool proxy=false){
        distant=proxy;joints.clear();joints.reserve(120);
        const int s=proxy?6:14;
        const int root=add(-1,{0,0,0});
        const int hips=hip=add(root,{0,.83,0},{.007,.014,.011});
        spine=add(hips,{0,1.09,0},{.018,.023,.027});
        const int chest=add(spine,{.015,1.38,0},{.014,.020,.016});
        neck=add(chest,{.035,1.70,0},{.025,.015,.028});
        head=add(neck,{-.035,1.91,.015});
        g(hips).stone({0,.89,0},.145,NorthMetalTexture,iron,6,s);
        for(int i=0;i<11;i++){
            Vec c{.014*sin(i*.8),.94+i*.067,-.023};
            auto& b=g(i<3?hips:i<7?spine:chest);
            b.stone(c,.057,NorthMetalTexture,edge,5,s);
            b.tube(c+Vec{-.046,0,.02},c+Vec{.046,0,.02},.012,NorthGlowTexture,violet,5);
        }
        for(int sign:{-1,1}){
            for(int i=0;i<8;i++){
                const double y=1.60-i*.075, w=.125+.075*sin((i+1)*Pi/10);
                Vec last{0,y,-.075};
                for(int j=1;j<=s;j++){
                    double a=Pi*j/s;Vec p{sign*w*sin(a),y-.030*sin(a),-.075*cos(a)};
                    g(chest).tube(last,p,.014-i*.00045,NorthMetalTexture,bone,6);last=p;
                }
            }
            // Tensioned internal conduits and violet pinpricks in the cavity.
            for(int i=0;i<(proxy?5:15);i++){
                Vec last{sign*(.025+.10*rnd(21+i)),1.65,-.035+.05*rnd(71+i)};
                for(int j=1;j<=7;j++){
                    Vec p{sign*(.045+.105*sin(j*.8+i)*.6),1.65-j*.111,.045*sin(i+j*.8)};
                    g(chest).tube(last,p,.004+(.003*rnd(i+88)),NorthMetalTexture,cord,5);
                    if((i+j)%7==0)g(chest).stone(p,.011,NorthGlowTexture,violet,3,5);
                    last=p;
                }
            }
            // Independent arms, palms and three segments per claw.
            Vec a{sign*.235,1.59,0},b{sign*.37,1.10,.045},c{sign*.40,.70,.13};
            int shoulder=shoulders[sign>0]=add(chest,a,{.033,sign*.025,sign*.038});
            int elbow=add(shoulder,b,{.037,.026,sign*.027});
            int wrist=add(elbow,c,{.065,sign*.045,.038});
            auto limb=[&](int joint,Vec p,Vec q,double r){
                auto& m=g(joint);m.tube(p,q,r,NorthMetalTexture,edge,s);
                m.stone(p,r*1.4,NorthMetalTexture,bone,6,s);
                m.stone(q,r*1.22,NorthMetalTexture,iron,5,s);
                for(int k=0;k<4;k++){
                    double t=.14+k*.19;Vec u=p+(q-p)*t;
                    m.tube(u,u+(q-p)*.075,r*1.15,NorthMetalTexture,edge,s);
                }
                m.tube(p+Vec{0,0,r*.87},q+Vec{0,0,r*.87},r*.14,NorthMetalTexture,dark,5);
            };
            limb(shoulder,a,b,.032);limb(elbow,b,c,.025);
            g(wrist).tube(c,c+Vec{0,-.085,.012},.035,NorthMetalTexture,iron,s);
            for(int f=0;f<5;f++){
                Vec p=c+Vec{(f-2)*.021,-.065,.016};
                const double len=.055+.022*sin((f+.4)*Pi/5);
                int parent=wrist;
                for(int k=0;k<3;k++){
                    int j=add(parent,p,{.060+k*.022,0,sign*.018});
                    Vec q=p+Vec{(f-2)*.004-sign*.008*k,-len*(1.-k*.16),.016+k*.019};
                    g(j).tube(p,q,.009-k*.0016,NorthMetalTexture,bone,6);
                    g(j).stone(p,.011-k*.001,NorthMetalTexture,edge,4,6);
                    parent=j;p=q;
                }
            }
            // Narrow legs inside the split robe, with a stable grounded foot.
            Vec h{sign*.095,.88,0},k{sign*.105,.45,.01},ank{sign*.12,.075,0};
            int leg=thighs[sign>0]=add(hips,h),shin=shins[sign>0]=add(leg,k),foot=feet[sign>0]=add(shin,ank);
            limb(leg,h,k,.036);limb(shin,k,ank,.027);
            g(foot).tube(ank,{sign*.12,.052,.13},.045,NorthMetalTexture,iron,8);
        }
        // Hollow faceplate: actual missing triangles expose the shadowed interior.
        auto& mask=g(head);
        mask.stone({-.035,1.94,.015},.137,NorthMetalTexture,dark,proxy?8:18,proxy?12:30);
        const int rows=proxy?30:92,cols=proxy?36:128;
        auto face=[&](double u,double v){
            double w=.134*(1.-.14*v-.25*v*v)*(1.+.055*sin(v*8));
            return Vec{-.035+w*u+.012*sin(v*2.4),1.94+.188*v,.035+.110*sqrt(std::max(0.,1.-u*u))+.027*(1.-v*v)+.008*exp(-pow(u/.2,2)-pow((v+.02)/.32,2))+.002*sin(u*18+v*11)+.003*sin(u*31-v*17)*sin(v*22)};
        };
        auto hole=[](double u,double v){
            double a=(u+.37+.19*v)/.29,b=(v-.23+.23*(u+.37))/.125;
            double c=(u-.34+.13*v)/.25,d=(v-.30-.30*(u-.34))/.12;
            double e=(u-.13*sin(v*8))/.20,f=(v+.45)/.34;
            double jag=.10*sin(u*47+v*29)+.065*sin(v*79-u*23);
            return a*a+b*b<1.+jag || c*c+d*d<1.+jag || e*e+f*f<1.+jag || (v<-.12&&std::abs(u-.42*v+.025*sin(v*36))<.038);
        };
        for(int y=0;y<rows;y++)for(int x=0;x<cols;x++){
            double u=-1.+2.*x/cols,v=-1.+2.*y/rows,U=u+2./cols,V=v+2./rows;
            const double uc=(u+U)*.5,vc=(v+V)*.5;
            auto solid=[&](double x,double y){return x*x+y*y*.83<=1. && !hole(x,y) && x>=-1 && x<=1 && y>=-1 && y<=1;};
            if(!solid(uc,vc))continue;
            float tint=float(.85+.12*sin(uc*12+vc*17)*sin(vc*8)+.025*rnd(x+y*cols+118));
            if(hole(uc+.045,vc)||hole(uc-.045,vc)||hole(uc,vc+.045)||hole(uc,vc-.045))tint*=.45f;
            const size_t first=mask.points.size();
            mask.quad(face(u,v),face(U,v),face(U,V),face(u,V),NorthMaskTexture,{bone.r*tint,bone.g*tint,bone.b*tint});
            const std::array<std::array<double,2>,6> uv={{{u,v},{U,v},{U,V},{u,v},{U,V},{u,V}}};
            if(mask.points.size()==first+6)for(size_t k=0;k<6;k++){
                auto& vert=mask.points[first+k];double x=uv[k][0],y=uv[k][1];
                Vec n=unit(cross(face(x+.001,y)-face(x-.001,y),face(x,y+.001)-face(x,y-.001)));
                vert.nx=n.x;vert.ny=n.y;vert.nz=n.z;vert.u=float(x*.5+.5);vert.v=float(y*.5+.5);
            }
            // The chipped edges have real thickness; only actual boundary cells
            // get a sidewall, leaving the holes open and avoiding layered faces.
            auto lip=[&](Vec a,Vec b){mask.quad(a,b,b-Vec{0,0,.009},a-Vec{0,0,.009},NorthMaskTexture,edge);};
            if(!solid(uc-2./cols,vc))lip(face(u,V),face(u,v));
            if(!solid(uc+2./cols,vc))lip(face(U,v),face(U,V));
            if(!solid(uc,vc-2./rows))lip(face(u,v),face(U,v));
            if(!solid(uc,vc+2./rows))lip(face(U,V),face(u,V));
        }
        if(!proxy){
            // Dark hairline fractures drawn as tiny physical incisions with irregular branches.
            for(int crack=0;crack<12;crack++){
                double u=-.83+1.66*rnd(820+crack),v=.90-.21*rnd(111+crack);
                for(int k=0;k<6;k++){
                    double U=u+.045*(rnd(92+crack*41+k)*2-1),V=v-.035-.040*rnd(120+crack*29+k);
                    if(u*u+v*v*.83<.95 && U*U+V*V*.83<.95 && !hole(u,v)&&!hole(U,V))
                        mask.tube(face(u,v)+Vec{0,0,.001},face(U,V)+Vec{0,0,.001},.0011,NorthMetalTexture,cord,4);
                    u=U;v=V;
                }
            }
        }
        for(auto uv:{Vec{-.37,.23,0},Vec{.34,.30,0},Vec{.09,-.36,0},Vec{-.02,-.58,0}}){
            Vec p=face(uv.x,uv.y)-Vec{0,0,.025};
            mask.stone(p,.010,NorthGlowTexture,bright,5,10);
            mask.stone(p-Vec{0,0,.012},.023,NorthMetalTexture,cord,5,10);
        }
        // Hair: each strand chain bends at successive hanging segments.
        for(int i=0;i<(proxy?15:42);i++){
            double a=Pi*.35+2*Pi*i/(proxy?15:42);
            // Start beneath a swept scalp cover, rather than straight spikes.
            Vec p{-.035+.119*cos(a),2.01+.052*rnd(i+30),.015+.110*sin(a)};
            const int count=proxy?2:4;int par=head;
            // Thick, smoothly curved wet locks lead from the crown into the hanging chains.
            Vec root{-.035+.038*cos(a),2.112+.010*sin(a),.024+.043*sin(a)};
            Vec last=root;
            for(int k=1;k<=8;k++){
                const double t=k/8.;Vec q=root+(p-root)*t+Vec{.027*sin(Pi*t)*cos(a),.022*sin(Pi*t),.031*sin(Pi*t)*sin(a)};
                g(head).tube(last,q,(proxy?.010:.011)*(1.-.18*t),NorthMetalTexture,cord,proxy?5:8);last=q;
            }
            for(int j=0;j<count;j++){
                const int id=add(par,p,{.026+.012*rnd(i),.025,.047});
                Vec q=p+Vec{.016*sin(i*.7+j*1.7)+.010*cos(a),-(.19+.085*rnd(i+230))* (proxy?1.7:1.),-.010+.021*sin(j*1.7+i)};
                Vec last=p;
                for(int k=1;k<= (proxy?2:4);k++){
                    const double t=double(k)/(proxy?2:4);Vec v=p+(q-p)*t+Vec{.009*sin(t*Pi)*sin(a+j),0,.008*sin(t*Pi)*cos(a+j)};
                    g(id).tube(last,v,(.005+.004*rnd(i+432))*(1.-j*.12-.05*t),NorthMetalTexture,cord,proxy?4:7);last=v;
                }
                if(!proxy)g(id).tube(p+Vec{.009,0,.003},q+Vec{.011,0,.004},.0024,NorthMetalTexture,dark,4);
                p=q;par=id;
            }
        }
        // Ritual halo stays on the upper torso, not rigidly glued to the head.
        const int halo=add(chest,{0,1.99,-.15},{.010,.025,.014});
        auto ring=[](double a){return Vec{.395*cos(a),1.98+.395*sin(a),-.16};};
        for(int i=0;i<48;i++){
            double a=2*Pi*i/48,b=2*Pi*(i+1)/48;
            g(halo).tube(ring(a),ring(b),.017,NorthMetalTexture,edge,7);
            if(i%3==0){
                g(halo).stone(ring(a),.025,NorthMetalTexture,bone,4,7);
                const Vec axis=unit(ring(a)-Vec{0,1.98,-.16});
                if(i%9==0 || i==15)g(halo).tube(ring(a),ring(a)+axis*(.065+.06*rnd(i+85)),.007,NorthMetalTexture,edge,6);
                if(!proxy)for(int wrap=-2;wrap<=2;wrap++){
                    const double v=a+wrap*.012;
                    g(halo).tube(ring(v)-axis*.009,ring(v)+axis*.014,.004,NorthMetalTexture,cord,5);
                }
            }
        }
        for(int i=0;i<5;i++){
            double a=std::array<double,5>{.70,1.47,2.45,3.10,3.89}[i];Vec p=ring(a);
            int tag=add(halo,p,{.075,.085,.065});
            Vec end=p+Vec{0,-.085,0};g(tag).tube(p,end,.0035,NorthMetalTexture,cord,5);
            const double len=.19+.075*rnd(i+65);
            g(tag).quad(end+Vec{-.042,0,0},end+Vec{.042,0,0},end+Vec{.035,-len,.005},end+Vec{-.046,-len,0},NorthClothTexture,cloth);
            for(int z=0;z<4;z++){
                Vec m=end+Vec{0,-.029-z*.043,.003};
                g(tag).tube(m+Vec{-.022,.008,0},m+Vec{.019,-.01,0},.0028,NorthMetalTexture,violet,4);
                g(tag).tube(m+Vec{0,.017,0},m+Vec{-.009,-.017,0},.0027,NorthMetalTexture,violet,4);
            }
        }
        // Perforated hanging panels, each attached to a short cloth bone chain.
        for(int panel=0;panel<10;panel++){
            double a=2*Pi*(panel+.5)/10;
            int par=chest;
            const int steps=proxy?4:7,across=proxy?5:12,down=proxy?4:7;
            auto pt=[&](double t,double u){
                double r=.25+.075*t+.24*t*t*t+(.026*sin(u*9+panel*.7)+.012*sin(u*17-t*7))*sin(t*Pi);
                double ang=a+u*.40+.09*sin(t*5+panel);
                return Vec{r*cos(ang)+.035*sin(t*7+panel),1.62-1.62*t+.07*sin(u*6+panel)*t*t,r*.76*sin(ang)+.036*sin(t*10+u*4)};
            };
            for(int j=0;j<steps;j++){
                double t0=double(j)/steps;int id=add(par,pt(t0,0),{.004,.004,.006});par=id;
                for(int y=0;y<down;y++)for(int x=0;x<across;x++){
                    double t=(j+double(y)/down)/steps,T=t+1./(steps*down),u=-1.+2.*x/across,U=u+2./across;
                    const double mid=(u+U)*.5, row=(t+T)*.5;
                    if(y==0 && j>0)t-=.005;
                    if(y==down-1 && j<steps-1)T+=.005;
                    // Distinct elongated holes and ragged lower edge, not a painted hole.
                    if((panel==2||panel==7)&&row>.17&&row<.54&&std::abs(mid+.12*sin(row*14))<.53)continue;
                    if(row>.36&&!(panel==2&&row>.59&&row<.75)&&sin(mid*9+panel)*sin(row*24+panel*2)>.82)continue;
                    if(row>.83 && (sin(mid*13+panel*2)+.35*sin(row*40+panel))*.5+.5 < (row-.83)*3.4)continue;
                    const float shade=float(.84+.14*sin(mid*6+panel)*sin(row*13)+.022*rnd(panel*123+x*9+y*17+j*71));
                    const size_t first=g(id).points.size();
                    g(id).quad(pt(t,u),pt(t,U),pt(T,U),pt(T,u),NorthClothTexture,{cloth.r*shade,cloth.g*shade,cloth.b*shade});
                    const std::array<std::array<double,2>,6> uv={{{t,u},{t,U},{T,U},{t,u},{T,U},{T,u}}};
                    if(g(id).points.size()==first+6)for(size_t k=0;k<6;k++){
                        auto& vert=g(id).points[first+k];double y=uv[k][0],x=uv[k][1];
                        Vec n=unit(cross(pt(y,x+.001)-pt(y,x-.001),pt(y+.001,x)-pt(y-.001,x)));
                        if(dot(n,{vert.x,0,vert.z})<0)n=n*-1;
                        vert.nx=n.x;vert.ny=n.y;vert.nz=n.z;vert.u=float(x*.5+.5+panel);vert.v=float(y*2.);
                    }
                }
                if(panel==2 && j==int(steps*.68)){
                    const double center=(j+.5)/steps, r=.34;
                    auto sigil=[&](double a,double radius){return pt(center+sin(a)*.060*radius,cos(a)*r*radius)+Vec{0,0,.009};};
                    for(int k=0;k<24;k++)g(id).tube(sigil(k*2*Pi/24,1),sigil((k+1)*2*Pi/24,1),.0027,NorthMetalTexture,violet,5);
                    for(int k=0;k<3;k++)g(id).tube(sigil(Pi*.5+k*2*Pi/3,.89),sigil(Pi*.5+(k+1)*2*Pi/3,.89),.0027,NorthMetalTexture,violet,5);
                }
                if(!proxy)for(double u:{-1.,1.}){
                    g(id).tube(pt(t0,u),pt(double(j+1)/steps,u),.003,NorthMetalTexture,cord,4);
                    for(int stitch=0;stitch<5;stitch++){
                        const double t=t0+(stitch+.2)/(steps*5.);
                        Vec p=pt(t,u*.94),q=pt(t+.008,u*.85);
                        const Vec outward=unit(Vec{p.x,0,p.z})*.002;
                        g(id).tube(p+outward,q+outward,.0017,NorthClothTexture,edge,4);
                    }
                }
            }
        }
        // Collar folded around the upper chest and exposed ribs.
        for(int i=0;i<32;i++){
            auto p=[](double a,double r,double y){return Vec{r*cos(a),y-.055*sin(a),r*.62*sin(a)};};
            double a=2*Pi*i/32,b=2*Pi*(i+1)/32;
            g(chest).quad(p(a,.105,1.75),p(b,.105,1.75),p(b,.30,1.57),p(a,.30,1.57),NorthClothTexture,cloth);
            g(chest).tube(p(a,.30,1.57),p(b,.30,1.57),.007,NorthClothTexture,edge,6);
        }
        for(int layer=0;layer<3;layer++){
            auto fold=[&](double a,double t){
                const double radius=.12+.19*t+layer*.008;
                return Vec{radius*cos(a),1.76-.07*layer-.13*t-.08*std::max(0.,sin(a))*t+.014*sin(3*a+layer),radius*.71*sin(a)+.005*layer};
            };
            const int count=proxy?18:48;
            for(int i=0;i<count;i++){
                const double a=Pi*.05+Pi*1.03*i/count,b=Pi*.05+Pi*1.03*(i+1)/count;
                g(chest).quad(fold(a,.1),fold(b,.1),fold(b,.93),fold(a,.93),NorthClothTexture,{cloth.r*(.91f-layer*.06f),cloth.g*(.91f-layer*.06f),cloth.b*(.91f-layer*.06f)});
                g(chest).tube(fold(a,.93),fold(b,.93),.003,NorthClothTexture,edge,5);
            }
        }
        vertexCount=uniqueCount=0;indices.clear();
        for(auto& j:joints){
            std::unordered_map<std::array<unsigned,12>,unsigned,VertexHash> index;
            j.order.reserve(j.mesh.points.size());
            for(const Vertex& v:j.mesh.points){
                const auto key=std::bit_cast<std::array<unsigned,12>>(v);
                auto [it,inserted]=index.emplace(key,unsigned(j.unique.size()));
                if(inserted)j.unique.push_back(v);
                j.order.push_back(it->second);
            }
            for(auto index:j.order)indices.push_back(unsigned(uniqueCount)+index);
            uniqueCount+=j.unique.size();vertexCount+=j.order.size();
        }
    }
    void pose(Geometry& out,double time,double stride=0,double walking=0,bool indexed=true,bool compact=false) const{
        out.points.clear();out.points.reserve(compact?uniqueCount:vertexCount);
        struct Frame{Vec x,y,z,offset;Vec vector(Vec p)const{return x*p.x+y*p.y+z*p.z;} Vec point(Vec p)const{return vector(p)+offset;}};
        std::vector<Frame> transforms;transforms.reserve(joints.size());
        // Brief irregular catches followed by a slower recovery rather than constant shaking.
        const double window=fmod(std::max(0.,time),6.7);
        const double twitch=window>4.8?exp(-(window-4.8)*6.)*sin((window-4.8)*37.):0.;
        for(size_t i=0;i<joints.size();i++){
            const auto& j=joints[i];const double phase=time*1.3+i*.371;
            Vec a{j.motion.x*sin(phase),j.motion.y*sin(phase*.73),j.motion.z*sin(phase*.91)};
            if(int(i)==head)a={.08*sin(time*.57)+.13*twitch,.13*sin(time*.31)+.20*twitch,-.32+.095*sin(time*.63)+.29*twitch};
            if(int(i)==neck)a.z=-.07+.025*sin(time*.8);
            for(int side=0;side<2;side++){
                const double gait=sin(stride+side*Pi)*walking;
                if(int(i)==thighs[side])a.x=.34*gait;
                if(int(i)==shins[side])a.x=.39*std::max(0.,-gait);
                if(int(i)==feet[side])a.x=-.22*gait;
                if(int(i)==shoulders[side])a.x-=.16*gait;
            }
            Frame f{rot({1,0,0},a),rot({0,1,0},a),rot({0,0,1},a),j.pivot-rot(j.pivot,a)};
            if(j.parent>=0){auto& p=transforms[j.parent];f={p.vector(f.x),p.vector(f.y),p.vector(f.z),p.point(f.offset)};}
            transforms.push_back(f);
            auto transform=[&](Vertex v){Vec p=f.point({v.x,v.y,v.z}),n=f.vector({v.nx,v.ny,v.nz});v.x=p.x;v.y=p.y;v.z=p.z;v.nx=n.x;v.ny=n.y;v.nz=n.z;return v;};
            if(!indexed){for(auto v:j.mesh.points)out.points.push_back(transform(v));continue;}
            if(compact){for(auto v:j.unique)out.points.push_back(transform(v));continue;}
            transformed.resize(j.unique.size());
            std::transform(j.unique.begin(),j.unique.end(),transformed.begin(),transform);
            for(auto k:j.order)out.points.push_back(transformed[k]);
        }
    }
};
}
