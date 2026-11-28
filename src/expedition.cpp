#include "expedition.hpp"
#include <limits>

namespace expedition {
namespace {
constexpr int Width=micro::Width,Depth=micro::Depth;
constexpr int Unreachable=100000;
bool valid(int x,int z){return x>0 && z>0 && x<Width-1 && z<Depth-1;}
int index(int x,int z){return z*Width+x;}
constexpr std::array<Cell,4> Cross={{{-1,0,0},{1,0,0},{0,0,-1},{0,0,1}}};

double footing(const micro::Area& area,Vec point,double fallback){
    const auto column=floorCell(point);
    const auto* roof=area.blocks().roof(column);
    if(roof && std::abs(roof->high()-fallback)<1.95){
        // A point sample places a wide creature inside the uphill side of a
        // slope. Use the highest corner of the actual walking footprint.
        constexpr double r=.62*.24;
        double top=-1e9;
        for(double dx:{-r,r})for(double dz:{-r,r}){
            const Vec q=point+Vec{dx,0,dz};const Cell c=floorCell(q);
            const auto* cap=area.blocks().roof(c);
            if(cap && std::abs(cap->high()-fallback)<1.95)
                top=std::max(top,cap->height(q.x-c.x,q.z-c.z));
            else top=std::max(top,fallback-.002);
        }
        return top+.006;
    }
    return fallback;
}

void ribbon(Geometry& g,Vec a,Vec b,Vec c,Vec d,int id,Color tint){
    g.quad(a,b,c,d,id,tint);
    g.quad(d,c,b,a,id,tint);
}
void coat(Geometry& g,double a,double b,double waist,double spread,int material,Color tint){
    constexpr int sides=8;
    auto ring=[](int n,double y,double width){
        const double angle=(n+0.5)*2*Pi/sides;
        return Vec{width*std::sin(angle),y,width*0.60*std::cos(angle)};
    };
    for(int i=0;i<sides;i++)
        g.quad(ring(i,a,waist),ring(i+1,a,waist),ring(i+1,b,spread),ring(i,b,spread),material,tint);
}
}

void Wildlife::survey(const micro::Area& area){
    ground.fill(-1);
    for(int z=1;z<Depth-1;z++)for(int x=1;x<Width-1;x++){
        for(int h=2;h<=11;h++){
            const Cell floor{x,h-1,z};
            if(area.at(floor) && !furniture(area.at(floor)) &&
                (area.at({x,h,z})==Air || area.at({x,h,z})==OpenDoor) && !area.at({x,h+1,z})){
                ground[index(x,z)]=h;
                break;
            }
        }
    }
    revision=area.revision;
    destination={-1,-1,-1};
}
void Wildlife::route(Cell goal){
    destination=goal;
    distance.fill(Unreachable);
    if(!valid(goal.x,goal.z) || ground[index(goal.x,goal.z)]<0)return;
    std::array<Cell,Width*Depth> queue;
    int first=0,last=0;
    distance[index(goal.x,goal.z)]=0;
    queue[last++]=goal;
    while(first<last){
        Cell cell=queue[first++];
        const int here=index(cell.x,cell.z);
        for(auto step:Cross){
            const Cell next=cell+step;
            if(!valid(next.x,next.z))continue;
            const int there=index(next.x,next.z);
            if(ground[there]<0 || std::abs(ground[there]-ground[here])>1 || distance[there]!=Unreachable)continue;
            distance[there]=distance[here]+1;
            queue[last++]=next;
        }
    }
}
void Wildlife::prepare(const micro::Area& area){
    if(area.revision!=revision)survey(area);
    if(initialized)return;
    initialized=true;
    route(floorCell(micro::Arrival+Vec{0,0,5}));
    std::vector<Cell> candidates;
    for(int z=12;z<Depth-5;z++)for(int x=3;x<Width-3;x++){
        const int at=index(x,z);
        if(ground[at]<0 || distance[at]==Unreachable || (area.level==Village && z<29 && std::abs(x-24)<11))continue;
        candidates.push_back({x,ground[at],z});
    }
    if(candidates.size()<Hostiles){
        candidates.clear();
        for(int z=11;z<Depth-2;z++)for(int x=2;x<Width-2;x++)
            if(ground[index(x,z)]>0)candidates.push_back({x,ground[index(x,z)],z});
    }
    for(int i=0;i<Hostiles;i++){
        bool placed=false;
        while(!candidates.empty()){
            const auto pick=mix(std::uint32_t(421*i+area.level*1619+area.family*3251))%candidates.size();
            const auto point=candidates[pick];
            candidates[pick]=candidates.back();candidates.pop_back();
            Vec position=point.v()+Vec{.5,.01,.5};
            if(area.overlaps(position,.62))continue;
            const double down=area.sweep(position,.62,1,-1.95);
            if(down<=-1.949)continue;
            position.y+=down+.006;
            if(area.overlaps(position,.62))continue;
            enemies[i].feet=position;placed=true;break;
        }
        if(!placed){defeated.set(i);continue;}
        enemies[i].yaw=Pi;
    }
    for(std::size_t i=0;i<elves.size();i++){
        const int x=20+int(i%2)*8,z=13+int(i/2)*10;
        const int h=ground[index(x,z)];
        elves[i].feet={x+0.5,double(h>0?h:5)+0.002,z+0.5};
        elves[i].yaw=i%2?-1.3:1.3;
    }
}
int Wildlife::advance(const micro::Area& area,Vec player,double size,double dt,std::optional<Vec> predator){
    prepare(area);
    const Cell goal=floorCell(player);
    if(goal.x!=destination.x || goal.z!=destination.z)route(goal);
    int attacks=0;
    for(int id=0;id<Hostiles;id++){
        if(defeated[id])continue;
        auto& mob=enemies[id];
        const Vec to=player-mob.feet;
        const double separation=std::hypot(to.x,to.z);
        const bool afraid=predator && length(mob.feet-*predator)<7.0;
        mob.attack=std::max(0.0,mob.attack-dt);
        if(!afraid && separation<0.55+size*0.32 && std::abs(to.y)<1.0 && mob.attack==0){
            const auto mouth=mob.feet+Vec{0,0.78,0};
            const auto target=player+Vec{0,0.82*size,0}-mouth;
            if(!area.ray(mouth,unit(target),length(target))){
                mob.attack=0.95;
                ++attacks;
            }
        }
        if(!afraid && (separation<0.48 || separation>13.0+(id%4)*1.25))continue;
        const Cell cell=floorCell(mob.feet);
        if(!valid(cell.x,cell.z))continue;
        Cell next=cell;
        int best=distance[index(cell.x,cell.z)];
        double safeDistance=afraid?length(Vec{cell.x+.5,mob.feet.y,cell.z+.5}-*predator):0.;
        for(auto side:Cross){
            const Cell neighbor=cell+side;
            if(!valid(neighbor.x,neighbor.z))continue;
            const auto at=index(neighbor.x,neighbor.z);
            if(ground[at]<0 || std::abs(ground[at]-ground[index(cell.x,cell.z)])>1)continue;
            if(afraid){
                const double d=length(Vec{neighbor.x+.5,mob.feet.y,neighbor.z+.5}-*predator);
                if(d>safeDistance){safeDistance=d;next=neighbor;}
            }else if(distance[at]<best){best=distance[at];next=neighbor;}
        }
        if(next==cell && (afraid || goal.x!=cell.x || goal.z!=cell.z))continue;
        const Vec aim=next==cell?player:Vec{next.x+0.5,player.y,next.z+0.5};
        const Vec move=unit(Vec{aim.x-mob.feet.x,0,aim.z-mob.feet.z})*std::min((afraid?2.65:2.1)*dt,afraid?std::hypot(aim.x-mob.feet.x,aim.z-mob.feet.z):separation);
        const double height=footing(area,mob.feet+move,ground[index(next.x,next.z)]+0.002);
        const double rise=std::max(0.0,height-mob.feet.y);
        if(rise>0)mob.feet.y+=area.sweep(mob.feet,0.62,1,rise);
        for(int axis:{0,2})mob.feet[axis]+=area.sweep(mob.feet,0.62,axis,move[axis]);
        const Cell moved=floorCell(mob.feet);
        if(valid(moved.x,moved.z)){
            const double drop=footing(area,mob.feet,ground[index(moved.x,moved.z)]+0.002)-mob.feet.y;
            if(drop<0)mob.feet.y+=area.sweep(mob.feet,0.62,1,std::max(-dt*7,drop));
        }
        mob.yaw=std::atan2(move.x,move.z);
        mob.stride+=dt*8;
    }
    return attacks;
}
bool Wildlife::hit(Vec eye,Vec ray,double range,double& contact,int damage){
    int closest=-1;
    contact=range;
    for(int i=0;i<Hostiles;i++){
        if(defeated[i])continue;
        const auto offset=enemies[i].feet+Vec{0,0.79,0}-eye;
        const double depth=dot(offset,ray);
        if(depth<0 || depth>=contact)continue;
        const auto residual=offset-ray*depth;
        if(residual.x*residual.x/0.17+residual.z*residual.z/0.17+residual.y*residual.y/0.66>1)continue;
        closest=i;contact=depth;
    }
    if(closest<0)return false;
    auto& mob=enemies[closest];
    mob.health-=damage;
    if(mob.health<=0)defeated.set(closest);
    return true;
}

void humanoid(Geometry& g,bool elf,int variant){
    const Color steel{0.24f,0.36f,0.43f},shadow{0.40f,0.29f,0.48f},
        skin=elf?Color{0.95f,0.93f,0.98f}:Color{0.73f,0.92f,0.63f},
        gold{1.25f,1.02f,0.52f};
    const std::array dyes={Color{0.39f,0.82f,0.63f},Color{0.58f,0.47f,1.03f},Color{0.94f,0.69f,0.31f},Color{0.44f,0.75f,1.02f}};
    const auto cloth=elf?dyes[variant%4]:shadow;
    for(double side:{-1.,1.}){
        const double x=side*0.13;
        g.box({x-0.09,0.02,-0.12},{x+0.09,0.15,0.20},LeatherTexture,{1.12f,0.95f,0.87f});
        g.tube({x,0.13,0},{x*0.9,0.65,0.02},0.086,ClothTexture,elf?steel:shadow,6);
        const Vec shoulder{side*0.28,1.13,0.02},elbow{side*0.38,0.93,elf?0.02:0.30},
            hand{side*(elf?0.34:0.38),elf?0.78:0.94,elf?0.035:0.52};
        g.stone(shoulder,0.14,Steel,cloth,4,7);
        g.tube(shoulder,elbow,0.089,ClothTexture,cloth,7);
        g.tube(elbow,hand,0.070,ClothTexture,cloth,7);
        g.stone(hand,0.068,SkinTexture,skin,4,6);
        const Vec ear{side*0.15,1.48,0.02},tip{side*(elf?0.32:0.23),1.62,-0.035};
        ribbon(g,ear+Vec{0,-0.09,0},tip,ear+Vec{0,0.06,-0.067},ear+Vec{0,0,0.045},SkinTexture,skin);
        if(elf){
            g.box({x*0.53-0.025,1.477,0.155},{x*0.53+0.025,1.492,0.169},SkinTexture,{1.17f,1.25f,1.29f});
            g.stone({x*0.53,1.484,0.171},0.010,FluxTexture,{0.25f,0.56f,0.50f},4,7);
            g.tube({x*0.53-0.026,1.514,0.148},{x*0.53+0.026,1.506,0.157},0.008,LeatherTexture,{1,0.73f,0.69f},5);
            g.stone(ear+Vec{0,-0.065,0.03},0.031,BrassTexture,gold,4,6);
            g.tube(elbow+(hand-elbow)*0.71,elbow+(hand-elbow)*0.85,0.081,BrassTexture,gold,6);
        }else g.stone({x*0.50,1.48,0.148},0.026,Lamp,{1.15f,0.58f,0.33f},3,6);
    }
    coat(g,0.68,1.18,0.18,0.32,ClothTexture,cloth);
    coat(g,0.68,elf?0.40:0.50,0.19,0.29,ClothTexture,cloth);
    coat(g,0.67,0.73,0.20,0.205,BrassTexture,gold);
    g.stone({0,1.46,0.015},0.167,SkinTexture,skin,7,11);
    if(elf){
        g.stone({0,1.454,0.170},0.027,SkinTexture,skin,4,7);
        g.tube({-0.028,1.405,0.158},{0.028,1.405,0.158},0.007,SkinTexture,{0.58f,0.32f,0.29f},5);
    }
    g.stone({0,1.554,-0.035},0.169,elf?Timber:Steel,elf?gold:shadow,5,9);
    g.stone({0,1.01,0.194},0.055,elf?Socket:Lamp,elf?gold:cloth,4,7);
    if(elf){
        const Vec a{-0.24,1.13,-0.13},b{0.24,1.13,-0.13};
        ribbon(g,a,b,{0.25,0.52,-0.26},{-0.25,0.52,-0.26},Steel,cloth);
        for(double x:{-0.21,0.21})g.tube({x,1.10,-0.15},{x,0.55,-0.26},0.019,Copper,gold,4);
        g.tube({-0.155,1.55,0.091},{0.155,1.55,0.091},0.017,BrassTexture,gold,7);
        g.stone({0,1.55,0.115},0.034,FluxTexture,gold,4,7);
        for(double side:{-1.0,1.0})for(int line=0;line<4;line++){
            const double off=line*0.027;
            const Vec from{side*(0.11+off),1.577-off*0.17,-0.06};
            const Vec knot{side*(0.15+off*0.45),1.35,-0.123-off*0.19};
            g.tube(from,knot,0.022,Timber,variant==1?Color{0.51f,0.36f,0.33f}:gold,6);
            if(line<2)g.tube(knot,knot-Vec{0,0.22,0.038},0.016,Timber,gold,6);
        }
        if(variant%2==0){
            const Vec base{-0.40,0.18,0.055},tip{-0.40,1.67,0.08};
            g.tube(base,tip,0.032,Timber,gold,6);
            g.stone(tip+Vec{0,0.07,0},0.088,FluxTexture,{0.68f,1.12f,0.81f},5,9);
            g.tube(tip-Vec{0,0.05,0},tip+Vec{0,0.045,0},0.05,BrassTexture,gold,7);
        }
    }
}

Box furnitureVolume(int id,Vec p,bool folded){
    if(id==Door)return folded?Box{p+Vec{0.04,0,0.04},p+Vec{0.15,1,0.95}}
        :Box{p+Vec{0.04,0,0.44},p+Vec{0.96,1,0.56}};
    if(id==Seat)return {p+Vec{0.19,0,0.19},p+Vec{0.80,0.46,0.80}};
    if(id==Lantern)return {p+Vec{0.29,0,0.29},p+Vec{0.71,0.56,0.71}};
    return {p+Vec{0,0.045,0},p+Vec{1,folded?0.92:0.14,1}};
}

std::optional<Hit> furnitureRay(Vec start,Vec dir,double reach,const std::function<int(Cell)>& materialAt){
    Cell cell=floorCell(start),previous=cell,normal{};
    std::array<double,3> jump{},next{};
    std::array<int,3> sign{};
    for(int i=0;i<3;i++){
        if(std::abs(dir[i])<1e-12){jump[i]=next[i]=1e18;continue;}
        sign[i]=dir[i]>0?1:-1;
        jump[i]=1/std::abs(dir[i]);
        next[i]=(cell[i]+(sign[i]>0)-start[i])/dir[i];
    }
    double depth=0;
    while(depth<=reach){
        int axis=next[0]<next[1]?0:1;
        if(next[2]<next[axis])axis=2;
        const int id=materialAt(cell);
        if(next[axis]>depth+1e-9 && furniture(id)){
            const auto box=expedition::furnitureVolume(furnitureBase(id),cell.v(),id!=furnitureBase(id));
            if(const auto hit=intersectRay(start,dir,box,reach))
                return Hit{cell,previous,normal,0,id,hit->at,hit->normal.v()};
        }
        depth=next[axis];next[axis]+=jump[axis];
        previous=cell;normal={};normal[axis]=-sign[axis];cell[axis]+=sign[axis];
    }
    return {};
}

void decoration(Geometry& g,int id,Vec p,bool folded){
    const Color wood{0.83f,0.61f,0.44f},rim{1.0f,0.82f,0.56f},tile{0.70f,0.48f,0.48f};
    const auto box=[&](Vec a,Vec b,int m,Color tint){g.box(p+a,p+b,m,tint);};
    if(id==Door){
        for(double x:{0.04,0.91})box({x,0,0.44},{x+0.05,1,0.56},Timber,rim);
        box({0.06,0.93,0.44},{0.94,1,0.56},Timber,rim);
        const auto door=[&](Vec local){return p+(folded?Vec{0.09+local.z,local.y,0.04+local.x}:Vec{0.04+local.x,local.y,0.47+local.z});};
        const auto panel=[&](double left,double right,double bottom,double top){
            const Vec a=door({left,bottom,0.055}),b=door({right,bottom,0.055});
            const Vec c=door({right,top,0.055}),d=door({left,top,0.055});
            const Vec inset=folded?Vec{-0.016,0,0}:Vec{0,0,-0.016};
            const Vec along=folded?Vec{0,0,0.023}:Vec{0.023,0,0};
            const Vec low{0,0.025,0};
            const Vec e=a+along+low+inset,f=b-along+low+inset;
            const Vec h=d+along-low+inset,g2=c-along-low+inset;
            ribbon(g,a,b,f,e,Timber,rim);ribbon(g,b,c,g2,f,Timber,rim);
            ribbon(g,c,d,h,g2,Timber,rim);ribbon(g,d,a,e,h,Timber,rim);
            ribbon(g,e,f,g2,h,Timber,wood);
        };
        for(int n=0;n<5;n++){
            const double x=0.06+n*0.16;
            g.box(door({x,0.018,0}),door({x+0.158,0.919,0.058}),Timber,wood);
        }
        for(double j:{0.10,0.55})for(double i:{0.17,0.48})panel(i,i+0.23,j,j+0.29);
        for(double y:{0.22,0.75})g.tube(door({0.079,y,0.075}),door({0.079,y+0.09,0.075}),0.018,BrassTexture,rim,8);
        g.stone(door({0.73,0.50,0.078}),0.031,BrassTexture,rim,5,9);
    }else if(id==Seat){
        for(double x:{0.24,0.67})for(double z:{0.25,0.68}){
            const Vec foot=p+Vec{x,0.041,z},peg=p+Vec{0.5+(x-0.5)*0.81,0.403,0.5+(z-0.5)*0.81};
            g.tube(foot,peg,0.049,Timber,wood,7);
            g.stone(foot,0.040,Timber,rim,4,7);
        }
        box({0.19,0.38,0.19},{0.80,0.46,0.80},Timber,rim);
        for(int n=0;n<4;n++){
            const double x=0.235+n*0.14;
            g.tube(p+Vec{x,0.45,0.71},p+Vec{x,0.81+0.019*(n==1 || n==2),0.8},0.025,Timber,wood,6);
        }
        g.tube(p+Vec{0.225,0.817,0.79},p+Vec{0.50,0.868,0.82},0.047,Timber,rim,8);
        g.tube(p+Vec{0.50,0.868,0.82},p+Vec{0.774,0.817,0.79},0.047,Timber,rim,8);
    }else if(id==Lantern){
        box({0.30,0.02,0.30},{0.70,0.13,0.70},Steel,rim);
        box({0.34,0.13,0.34},{0.66,0.46,0.66},Lamp,{1.30f,1.04f,0.68f});
        for(double x:{0.31,0.64})for(double z:{0.31,0.64})box({x,0.10,z},{x+0.05,0.48,z+0.05},Steel,rim);
        box({0.29,0.45,0.29},{0.71,0.50,0.71},Steel,rim);
        g.stone(p+Vec{0.5,0.5,0.5},0.19,Steel,rim,3,7);
    }else{
        const bool roof=id==RoofTile;
        const int strips=roof?6:3;
        const int material=roof?int(RoofingTexture):int(Timber);
        const auto tint=roof?tile:wood;
        const double slope=folded?0.78:0;
        for(int i=0;i<strips;i++){
            const double x0=double(i)/strips,x1=double(i+1)/strips-0.014;
            const auto point=[&](double x,double z,double offset=0.0){
                return p+Vec{x,0.14+x*slope+offset,z};
            };
            const Vec a=point(x0,0.01),b=point(x0,0.99),c=point(x1,0.99),d=point(x1,0.01);
            if(roof){
                const double mid=(x0+x1)*0.5;
                const Vec e=point(mid,0.01,0.034),f=point(mid,0.99,0.034);
                ribbon(g,a,b,f,e,material,tint);ribbon(g,e,f,c,d,material,tint);
                for(double z:{0.18,0.46,0.74})g.tube(point(x0,z,0.003),point(x1,z,0.003),0.008,Clay,tile,4);
            }else{
                ribbon(g,a,b,c,d,material,tint);
                for(double z:{0.06,0.94})g.stone(point(x1-0.045,z,0.003),0.012,BrassTexture,rim,3,6);
            }
            const Vec thickness{0,0.09,0};
            ribbon(g,a-thickness,a,d,d-thickness,material,tint);
            ribbon(g,b-thickness,c-thickness,c,b,material,tint);
            ribbon(g,a-thickness,b-thickness,b,a,material,tint);
            ribbon(g,d-thickness,d,c,c-thickness,material,tint);
        }
    }
}

void villageRoof(Geometry& g,Vec cell,double ridge,bool detailed){
    const auto point=[&](double x,double z,double extra=0.0){
        return Vec{cell.x+x,10.59-0.41*std::abs(cell.z+z-ridge)+extra,cell.z+z};
    };
    const Color tile{0.96f,0.83f,0.80f};
    ribbon(g,point(0,0),point(1,0),point(1,1),point(0,1),RoofingTexture,tile);
    if(!detailed)return;
    const Color seam{0.83f,0.58f,0.48f};
    for(double z:{0.032,0.363,0.696}){
        ribbon(g,point(0,z,0.005),point(1,z,0.005),point(1,z+0.018,0.018),point(0,z+0.018,0.018),RoofingTexture,seam);
        ribbon(g,point(0,z+0.018,0.018),point(1,z+0.018,0.018),point(1,z+0.053,0.003),point(0,z+0.053,0.003),RoofingTexture,tile);
    }
}

Vec villageTreeCenter(int index){return {index/2?37.5:10.5,4.8,index%2?25.5:14.5};}
void villageTree(Geometry& g,int index,int family,bool detailed){
    const Color bark{0.66f,0.52f,0.38f},warm{1.02f,0.73f,0.50f},
        canopy=family?Color{0.72f,0.82f,0.59f}:Color{0.61f,0.94f,0.72f};
    const Vec base=villageTreeCenter(index);
    g.tube(base,base+Vec{0,9.5,0},2.15,Timber,bark,detailed?11:7);
    g.tube(base+Vec{0,8,0},base+Vec{0,15,0},1.45,Timber,bark,detailed?11:7);
    for(int i=0;i<7;i++){
        const double angle=i*2*Pi/7+base.z;
        const Vec branch{std::sin(angle),0,std::cos(angle)};
        if(detailed || i%2==0){
            g.tube(base+branch*0.5,base+branch*3.1+Vec{0,0.8,0},0.64,Timber,bark,detailed?6:4);
            g.tube(base+Vec{0,8.6,0},base+branch*4.2+Vec{0,13.2,0},0.52,Timber,bark,detailed?7:4);
        }
        g.stone(base+branch*4.1+Vec{0,13.6,0},3.0+(i%3)*0.45,Leaves,canopy,detailed?4:3,detailed?9:5);
    }
    if(detailed)for(int j=0;j<13;j++){
        const double angle=j*2*Pi/13;
        const Vec direction{std::sin(angle),0,std::cos(angle)};
        g.tube(base+direction*2.13+Vec{0,2.5,0},base+direction*2.12+Vec{0,7.9,0},0.065,Timber,warm,5);
    }
}

void villageStreet(Geometry& g){
    const Color bark{0.66f,0.52f,0.38f},warm{1.02f,0.73f,0.50f};
    for(double x:{10.5,37.5})for(double z:{14.5,25.5})
        g.tube({x-5.4,10.62,z},{x+5.4,10.62,z},0.090,Timber,warm,7);
    for(double z:{11.8,21.8,29.8}){
        for(int i=0;i<10;i++){
            const double x=19.7+i*0.95, width=0.40;
            const double y=8.32-0.85*std::sin(i*Pi/9);
            const Color flag=i%3==0?Color{0.80f,1.06f,0.70f}:i%3==1?Color{1.26f,0.65f,0.48f}:Color{0.66f,0.70f,1.04f};
            const Vec a{x,y,z},b{x+width,y,z};
            g.tube(a,b+Vec{0.55,0.025,0},0.018,Timber,warm,4);
            g.triangle(a,b,{x+width*0.5,y-0.62,z+0.062},Steel,flag);
            g.triangle(b,a,{x+width*0.5,y-0.62,z+0.062},Steel,flag);
        }
    }
    for(double z:{10.5,20.5,28.5})for(double x:{19.6,28.4}){
        const Vec p{x,5,z};
        g.tube(p,p+Vec{0,2.45,0},0.095,Timber,bark,6);
        g.tube(p+Vec{0,2.45,0},p+Vec{x<24?0.56:-0.56,2.45,0},0.075,Timber,warm,6);
        decoration(g,Lantern,p+Vec{x<24?0.08:-1.08,1.82,-0.5},false);
    }
}

void pulseSidearm(Geometry& g,Geometry& focus,Geometry& cartridge,Geometry& flash){
    const Color blueSteel{0.47f,0.66f,0.82f},slate{0.25f,0.32f,0.48f},
        brass{1.30f,1.08f,0.62f},glow{0.71f,1.26f,1.40f};
    const std::array<Vec,8> shell={{
        {-0.088,-0.132,0},{0.088,-0.132,0},{0.130,-0.069,0},{0.123,0.072,0},
        {0.079,0.114,0},{-0.079,0.114,0},{-0.123,0.072,0},{-0.130,-0.069,0}}};
    const auto contour=[&](int i,double z,double scale){return shell[i%8]*scale+Vec{0,0,z};};
    for(int i=0;i<8;i++){
        g.quad(contour(i,-0.18,0.86),contour(i+1,-0.18,0.86),contour(i+1,0.09,1),contour(i,0.09,1),
            Steel,blueSteel);
        g.quad(contour(i,0.09,1),contour(i+1,0.09,1),contour(i+1,0.40,0.80),contour(i,0.40,0.80),
            Steel,i%2?blueSteel:Color{0.59f,0.74f,0.90f});
        g.triangle({0,0,0.4},contour(i,0.4,0.8),contour(i+1,0.4,0.8),Steel,blueSteel);
        g.triangle({0,0,-0.18},contour(i+1,-0.18,0.86),contour(i,-0.18,0.86),Steel,blueSteel);
    }
    g.box({-0.083,-0.29,-0.15},{0.083,-0.06,0.075},LeatherTexture,{0.67f,0.72f,0.85f});
    g.box({-0.090,-0.298,-0.158},{0.090,-0.272,0.088},BrassTexture,brass);
    for(int i=0;i<6;i++){
        const double y=-0.275+i*0.032;
        g.box({-0.095,y,-0.16},{0.095,y+0.009,0.078},Steel,blueSteel);
    }
    for(double sign:{-1.,1.}){
        g.tube({sign*0.09,0.07,-0.13},{sign*0.10,0.09,0.34},0.023,Copper,brass,8);
        g.tube({sign*0.10,0.07,0.34},{sign*0.22,0.035,0.55},0.035,Steel,blueSteel,8);
        g.tube({sign*0.22,0.035,0.55},{sign*0.15,-0.04,0.67},0.040,Copper,brass,8);
        g.stone({sign*0.15,-0.04,0.68},0.062,Socket,glow,5,8);
        g.tube({sign*0.127,-0.015,-0.11},{sign*0.128,0.018,0.20},0.012,FluxTexture,glow,7);
        for(int i=0;i<4;i++)
            g.tube({sign*0.113,-0.07,0.02+i*0.075},{sign*0.113,0.04,0.055+i*0.075},0.011,Copper,brass,5);
    }
    g.stone({0,0.01,0.38},0.11,Socket,glow,5,9);
    g.box({-0.06,0.10,-0.12},{0.06,0.144,0.02},Steel,slate);
    for(int i=0;i<8;i++){
        const double a=i*Pi/4,b=(i+1)*Pi/4;
        focus.tube({0.09*std::cos(a),0.225+0.09*std::sin(a),-0.045},
            {0.09*std::cos(b),0.225+0.09*std::sin(b),-0.045},0.014,Copper,brass,6);
    }
    focus.stone({0,0.225,-0.045},0.016,Lamp,{1.5f,0.38f,0.34f},4,6);
    cartridge.tube({0,0,-0.12},{0,0,0.12},0.064,BrassTexture,brass,12);
    for(double z:{-0.10,-0.075,0.06})cartridge.tube({0,0,z},{0,0,z+0.013},0.071,Steel,blueSteel,10);
    cartridge.stone({0,0,0.115},0.052,Socket,glow,4,7);
    flash.stone({},0.115,Socket,glow,4,8);
    for(int i=0;i<6;i++){
        const double angle=i*Pi/3;
        const Vec branch{std::cos(angle)*0.27,std::sin(angle)*0.27,0.15};
        flash.tube({},branch,0.016,Socket,glow,5);
    }
}
}
