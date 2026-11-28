#include "micro.hpp"
#include "expedition.hpp"
#include "settlement.hpp"
#include "mesh-faces.hpp"
#include "edited-mesh.hpp"
#include "arboreal-mesh.hpp"
#include <stdexcept>

namespace micro {
namespace {
constexpr std::array<Layer,Total> layers={{
    {"Canopy Floor","Grass stems, seed pods and copper flecks",0.0018,Grass,CopperOre,{0.24f,0.41f,0.27f}},
    {"Root Labyrinth","Branching roots and exposed iron pockets",0.000072,Timber,IronOre,{0.23f,0.14f,0.11f}},
    {"Pore Caves","Open pores, clay bridges and coal nodules",0.0000031,Clay,Coal,{0.24f,0.15f,0.25f}},
    {"Quartz Vault","Crystal towers with lodestone veins",0.000000142,Stone,Lodestone,{0.19f,0.17f,0.33f}},
    {"Elderbough","Elven homes among ancient trees and quartz roots",0.0000000096,Grass,Quartz,{0.26f,0.32f,0.29f}},
    {"Lattice Galleries","Alternating mineral rows and copper terraces",0.00000000144,IronOre,CopperOre,{0.12f,0.18f,0.29f}},
    {"Atomic Grove","Volumetric atom sites and vacancies",0.000000000361,CopperOre,CopperOre,{0.044f,0.073f,0.127f}},
    {"The Marrowkeep","The last-scale goblin fortress",0.000000000361/4096.0,Stone,IronOre,{0.13f,0.12f,0.19f}},
}};
std::size_t index(Cell c) {return (std::size_t(c.z)*Height+c.y)*Width+c.x;}
double radius(double x,double z) {return std::hypot(x,z);}
}
const Layer& details(int level) {return layers.at(level);}
double parentScale(int level){return details(level).cellMeters/(level?details(level-1).cellMeters:1);}
Vec anchor(int family,int level){return level?Descent:Entrances[family];}
Vec toParent(int family,int level,Vec local){return anchor(family,level)+(local-Arrival)*parentScale(level);}
bool within(Cell p){return p.x>=0 && p.x<Width && p.y>=0 && p.y<Height && p.z>=0 && p.z<Depth;}

Area::Area(int source,int tier,std::optional<std::uint64_t> worldSeed,bool inhabited):family(source),level(tier),inhabited(inhabited),voxels(Width*Height*Depth),
    matter(std::function<int(Cell)>([this](Cell cell){
        if(!within(cell))return cell.y<0?int(Bedrock):int(Air);
        const int id=raw(cell);
        return furniture(id)?int(Air):id;
    })){

    const auto& layer=details(tier);
    const auto variation=worldSeed?mix(std::uint32_t(*worldSeed)^mix(std::uint32_t(*worldSeed>>32))^
        std::uint32_t(37*source+73*tier)):0;
    const int shiftX=int(variation%12),shiftZ=int((variation>>10)%13);
    const auto write=[&](Cell p,int id){if(within(p))voxels[index(p)]=std::uint8_t(id);};
    if(tier==Fortress){
        for(int z=0;z<Depth;z++)for(int x=0;x<Width;x++)for(int y=0;y<=4;y++)
            write({x,y,z},y==0?Bedrock:y==4 && x>=8 && x<=39 && z>=14 && z<=44?CampTile:Stone);
        if(inhabited)settlement::fortressTerrain(write);
        return;
    }
    std::array<int,Width*Depth> floors{};
    // Use shared corner heights. The portals, central route and existing
    // settlement footprints remain level, with a continuous approach slope.
    const auto flatDistance=[](double x,double z,double x0,double x1,double z0,double z1){
        return std::hypot(std::max({x0-x,0.,x-x1}),std::max({z0-z,0.,z-z1}));
    };
    const auto height=[&](double x,double z){
        double dist=std::max(0.,std::abs(x-24.5)-3.5);
        dist=std::min(dist,flatDistance(x,z,19,30,0,9));
        dist=std::min(dist,flatDistance(x,z,19,30,47,58));
        if(tier==expedition::Village)dist=std::min(dist,flatDistance(x,z,4,44,7,31));
        if(settlement::site(tier))dist=std::min(dist,tier==6?flatDistance(x,z,25,48,11,45):flatDistance(x,z,26,44,11,23));
        double t=std::clamp(dist/5.,0.,1.);t=t*t*(3-2*t);
        const double a=x+shiftX,b=z+shiftZ;
        const double rolling=1.12*std::sin(a*.15+b*.095+source*.7)+.68*std::cos(b*.19-a*.08+tier*.61)
            +.29*std::sin(a*.43-b*.32+tier)+.16*std::cos(a*.76+b*.62);
        return 5.+t*rolling;
    };
    for(int z=0;z<Depth;z++)for(int x=0;x<Width;x++){
        const int offset=x-24;
        const bool trail=std::abs(offset)<=2 || ((z<=7 || z>=49) && std::abs(offset)<=4);
        const double r=std::sin((x+shiftX)*0.23+(z+shiftZ)*0.15+family*0.6)*std::cos((z+shiftZ)*0.21-tier*0.37);
        const bool plaza=tier==expedition::Village && x>=5 && x<=42 && z>=8 && z<=30;
        int floor=(trail||plaza)?4:std::clamp(4+int(std::round((1+tier%3)*r)),2,8);
        const bool enclosure=settlement::site(tier) && ((tier==6 && x>=25 && x<=47 && z>=11 && z<=44) ||
            (x>=26 && x<=44 && z>=11 && z<=23));
        const bool organic=inhabited && tier!=6;
        TerrainRoof profile{};
        if(organic){
            profile={{height(x,z),height(x+1,z),height(x+1,z+1),height(x,z+1)}};
            // Keep flat engineered floors on integral voxels. Natural ground
            // elsewhere is rendered from precisely the same collision profile.
            if(!enclosure && !trail && !plaza){
                matter.defineRoof({x,0,z},profile);
                floor=int(std::ceil(profile.high()))-1;
            }else floor=4;
        }
        floors[z*Width+x]=floor;
        const auto seed=CellHash{}({x+130*source+shiftX,floor+76*tier,z+shiftZ});
        for(int y=0;y<=floor;y++){
            int material=y==0?Bedrock:y==floor?layer.ground:(tier==6?layer.ground:Stone);
            if(organic){
                const bool skin=y==floor || (!enclosure && !trail && !plaza && profile.covers(y));
                if(y>0 && skin)material=layer.ground;
                else if(y>0 && tier==0)material=Dirt;
                if(y>1 && !skin && y<floor-1 && seed%13<3 && !trail && !enclosure)material=layer.ore;
            }else{
                if(y>1 && seed%11<3 && !trail)material=layer.ore;
                if(tier==0 && y+1==floor)material=Dirt;
            }
            if(trail && y==floor)material=tier==6?Quartz:CampTile;
            if(plaza && y==floor && (z==13 || z==14 || z==24 || z==25))material=Timber;
            write({x,y,z},material);
        }
        if(x==0 || x==Width-1 || z==0 || z==Depth-1)continue;
        if(trail || plaza || (inhabited && enclosure))continue;

        if(tier==0){
            if(inhabited)continue; // connected growth below
            const bool trunk=(x+shiftX)%7==2 && (z+shiftZ)%8==3;
            const bool leaf=radius((x+shiftX+2)%7-4,(z+shiftZ+4)%8-5)<2.4;
            if(trunk)for(int y=floor+1;y<=floor+10;y++)write({x,y,z},Timber);
            if(leaf)for(int y=13;y<=15;y++)
                if(seed%5!=0)write({x,y,z},Leaves);
            if(seed%22==1)write({x,floor+1,z},source?Quartz:Clay);
        }else if(tier==1){
            const double nerve=std::sin(z*0.34+source*0.82)*3.4;
            const int groove=int(std::round(nerve));
            if(((x+groove+48)%13<3 && z%11<8) || (std::abs(offset)>9 && z%12==6)){
                for(int y=floor+1;y<=floor+5+(x%3);y++)
                    write({x,y,z},y==floor+2 && seed%6==0?layer.ore:Timber);
            }else if(seed%17==0)write({x,floor+1,z},layer.ore);
        }else if(tier==2){
            const double pore=radius(((x+shiftX)%11)-5,((z+shiftZ)%13)-6);
            if(pore<2.4)for(int y=floor+1;y<=floor+9;y++)
                write({x,y,z},((y+int(seed%4))%5==0)?Coal:Clay);
            if(!inhabited && (z==17 || z==33 || z==44) && std::abs(offset)>3)
                for(int y=9;y<11;y++)write({x,y,z},Clay);
        }else if(tier==3){
            const double crystal=std::max(std::abs((x+shiftX)%9-4),std::abs((z+shiftZ)%9-4));
            if(crystal<=2)for(int y=floor+1;y<=floor+int(11-3.5*crystal);y++)
                write({x,y,z},y<floor+3 && seed%4==0?Lodestone:Quartz);
        }else if(tier==4){
            int shell=(x/6)+(z/5)+source;
            if((x+z+source)%7<=2)for(int y=floor+1;y<=floor+2+(shell%6);y++)
                write({x,y,z},(shell%5==0 || y==floor+2)?Quartz:CopperOre);
        }else if(tier==5){
            if((x%6<2 && (z+shiftZ)%7<3) || ((x+shiftX)%11==4 && z%2==0)){
                for(int y=floor+1;y<=floor+8;y++){
                    if(inhabited || (y+z+source)%5!=1)write({x,y,z},y%3==0?CopperOre:IronOre);
                }
            }
        }else{
            const bool site=((x+shiftX)%3==1 && (z+shiftZ)%3==1) || ((x+shiftX)%3==2 && (z+shiftZ)%3==2);
            if(site)for(int y=5+(x%2);y<20;y+=3)
                if(seed%11!=0 || y%2)write({x,y,z},(x+z+source)%4==0?Quartz:CopperOre);
        }
    }
    if(inhabited && tier==0){
        // One deterministic connected tree per irregular grove site. Trunks
        // start at the local solid floor; branches and leaves are stamped
        // relative to the trunk, not to a fixed global height.
        const auto eligible=[&](Cell p){return within(p) && p.x>1 && p.x<Width-2 && p.z>1 && p.z<Depth-2 &&
            std::abs(p.x-24.5)>4.5 && !(p.x>=25 && p.x<=45 && p.z>=10 && p.z<=24);};
        const auto grow=[&](Cell p,int id){
            if(!eligible(p) || p.y<=floors[p.z*Width+p.x])return;
            const int prior=voxels[index(p)];
            if(prior==Air || (id==Timber && prior==Leaves))write(p,id);
        };
        const auto branch=[&](Cell a,Cell b){
            // Axis-connected steps, including when the branch turns diagonally.
            while(a!=b){
                const Cell d=b-a;
                int axis=0;if(std::abs(d.y)>std::abs(d[axis]))axis=1;if(std::abs(d.z)>std::abs(d[axis]))axis=2;
                grow(a,Timber);a[axis]+=d[axis]>0?1:-1;
            }
            grow(b,Timber);
        };
        for(int gz=5;gz<Depth-4;gz+=8)for(int gx=5;gx<Width-4;gx+=8){
            const auto key=mix(variation^std::uint32_t(gx*19349663u+gz*73856093u));
            const int x=gx+int(key%5)-2,z=gz+int((key>>5)%5)-2;
            if(!eligible({x,6,z}))continue;
            const int floor=floors[z*Width+x],h=8+int((key>>11)%5);
            // Extend the trunk below the lowest corner of the sloped soil.
            // Otherwise the next integer voxel can hover up to one cell
            // above the actual rendered slope.
            const auto* profile=matter.roof({x,0,z});
            const int root=profile?std::max(1,int(std::floor(profile->low()))):floor;
            for(int y=root;y<=floor;y++)write({x,y,z},Timber);
            Cell base{x,floor+1,z};
            Cell top{x+int((key>>16)%3)-1,std::min(Height-4,floor+h),z+int((key>>19)%3)-1};
            Cell fork{x,top.y-3,z};branch(base,fork);branch(fork,top);
            // A small root buttress reaches down to the neighboring soil.
            for(auto d:std::array<Cell,4>{{{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}}}){
                Cell r=base+d;if(!eligible(r))continue;
                r.y=floors[r.z*Width+r.x]+1;
                if(std::abs(r.y-base.y)<=1){
                    branch(base,r);
                    const auto* cap=matter.roof(r);
                    const int low=cap?std::max(1,int(std::floor(cap->low()))):r.y-1;
                    for(int y=low;y<r.y;y++)write({r.x,y,r.z},Timber);
                }
            }
            for(int k=0;k<4;k++){
                const double a=double(key&255)*2*Pi/256+k*2.39996323;
                Cell tip=top+Cell{int(std::round(std::cos(a)*2.4)),(k%3)-1,int(std::round(std::sin(a)*2.4))};
                branch(fork,tip);
                const double r=2.0+.45*((key>>(k*3))&3);
                for(int dz=-3;dz<=3;dz++)for(int dy=-2;dy<=2;dy++)for(int dx=-3;dx<=3;dx++){
                    Cell p=tip+Cell{dx,dy,dz};
                    if(dx*dx+dz*dz+dy*dy*1.7<=r*r && p.y>fork.y)grow(p,Leaves);
                }
            }
        }
        // A protected path may trim the tip of a branch. Keep only organic
        // cells connected to a planted trunk, so clipped crowns never float.
        std::vector<unsigned char> connected(voxels.size());std::vector<Cell> queue;
        for(int z=1;z<Depth-1;z++)for(int x=1;x<Width-1;x++)for(int y=1;y<Height;y++){
            const Cell p{x,y,z};if(raw(p)!=Timber || y>floors[z*Width+x])continue;
            connected[index(p)]=1;queue.push_back(p);
        }
        for(std::size_t i=0;i<queue.size();i++)for(auto step:Sides){
            const Cell q=queue[i]+step;if(!within(q) || connected[index(q)] || !arbor::treeMaterial(raw(q)))continue;
            connected[index(q)]=1;queue.push_back(q);
        }
        for(int z=1;z<Depth-1;z++)for(int x=1;x<Width-1;x++)for(int y=floors[z*Width+x]+1;y<Height;y++){
            Cell p{x,y,z};if(arbor::treeMaterial(raw(p)) && !connected[index(p)])write(p,Air);
        }
    }

    if(tier==expedition::Village){
        for(int cx:{10,37})for(int cz:{14,25}){
            const int face=cx<24?1:-1;
            const int doorX=cx+face*3;
            for(int dz=-3;dz<=3;dz++)for(int dx=-5;dx<=5;dx++){
                const int x=cx+dx,z=cz+dz;
                if(std::abs(dx)<=1 && std::abs(dz)<=1)
                    for(int y=5;y<=19;y++)write({x,y,z},Timber);
                write({x,4,z},Timber);
                if(std::abs(dx)==5 || std::abs(dz)==3)
                    for(int y=5;y<=7;y++){
                        if(x==doorX && dz==-3 && y<=6)continue;
                        if(y==6 && ((std::abs(dx)==5 && std::abs(dz)==2) ||
                            (std::abs(dz)==3 && std::abs(dx)==3)))continue;
                        write({x,y,z},Timber);
                    }
                if(std::abs(dx)>1 || std::abs(dz)>1){
                    const int roof=8+(3-std::abs(dz))/2;
                    write({x,roof,z},Clay);
                }
            }
            write({doorX,5,cz-3},Door);
            write({doorX-face,5,cz+2},Seat);
            write({doorX+face,5,cz-4},Lantern);
        }
    }

    if(inhabited){
        if(tier==6)matter.latticeFrom=5;
        settlement::hiddenSite(tier,write);
    }

}

int Area::raw(Cell p) const {
    if(!within(p))return p.y>=Height?Air:Bedrock;
    if(auto found=edits.find(p);found!=edits.end())return found->second;
    return voxels[index(p)];
}
int Area::at(Cell p) const {
    if(!within(p))return raw(p);
    const int id=raw(p);
    return furniture(id)?id:matter.sample(0,p).id;
}
bool Area::set(Cell p,int material) {
    if(!within(p) || p.y==0 || raw(p)==Bedrock || material<Air ||
       (material>Bedrock && !furniture(material)) || (at(p)==material && !matter.editedChildren(p)))return false;
    matter.apply({0,p,furniture(material)?int(Air):material});
    if(voxels[index(p)]==material)edits.erase(p);
    else edits[p]=material;
    changed(p);
    return true;
}

bool Area::setDetail(int detail,Cell p,int material,bool campMining,std::optional<SoilHeights> surface){
    if(detail<0 || detail>FineDepth)return false;
    if(!detail)return set(p,material);
    const auto root=parent(p,subdivisions(detail));
    if(!within(root) || root.y==0 || furniture(raw(root)) || material<Air || material>Bedrock)return false;
    const auto before=matter.composition(detail,p);
    if(before[Bedrock] || (!campMining && before[CampTile]) || before[Water])return false;
    const auto previous=matter.revision();
    matter.apply({detail,p,material,surface});
    if(matter.revision()==previous)return false;
    changed(root);
    return true;
}
std::int64_t Area::shovel(Vec point,int tier,int material,bool deposit,std::int64_t budget,Box player,Vec normal){
    const auto start=matter.edits().size();
    const auto units=matter.wholeStroke(point,tier,material,deposit,budget,[&](Leaf leaf){
        const auto root=parent(leaf.position,subdivisions(leaf.level));
        return within(root) && root.y>0 && !furniture(raw(root)) &&
            !furniture(raw(root+Cell{0,1,0})) && (!deposit || !player.intersects(leaf.bounds()));
    },normal);
    std::unordered_set<Cell,CellHash> dirty;
    for(auto i=start;i<matter.edits().size();i++){
        const auto& edit=matter.edits()[i];
        dirty.insert(parent(edit.position,subdivisions(edit.level)));
    }
    for(auto root:dirty)changed(root);
    return units;
}
void Area::changed(Cell root){
    ++revision;
    for(int dz=-1;dz<=1;dz++)for(int dx=-1;dx<=1;dx++){
        const Cell p=root+Cell{dx,0,dz};
        if(within(p))tileVersions[p.z/TileWidth*TilesX+p.x/TileWidth]=revision;
    }
}
namespace {
constexpr Vec RoomMinimum{1,0,1},RoomMaximum{Width-1.0,Height-1.0,Depth-1.0};
}

bool Area::overlaps(Vec pos,double size,double height) const {
    const auto body=World::playerBox(pos,size,height);
    for(int axis=0;axis<3;axis++)if(body.lo[axis]<RoomMinimum[axis] || body.hi[axis]>RoomMaximum[axis])return true;
    if(matter.overlaps(pos,size,height))return true;
    const auto lo=floorCell(body.lo),hi=floorCell(body.hi);
    for(int x=lo.x;x<=hi.x;x++)for(int y=lo.y;y<=hi.y;y++)for(int z=lo.z;z<=hi.z;z++){
        const Cell cell{x,y,z};
        const int material=raw(cell);
        if(furniture(material) &&
            body.intersects(expedition::furnitureVolume(furnitureBase(material),cell.v(),material!=furnitureBase(material))))return true;
    }
    return false;
}
double Area::sweep(Vec feet,double size,int axis,double distance,double height) const {
    if(std::abs(distance)<=1e-12)return 0;
    const Box body=World::playerBox(feet,size,height);
    distance=std::clamp(distance,RoomMinimum[axis]-body.lo[axis],RoomMaximum[axis]-body.hi[axis]);
    Box span=body;
    if(distance>0)span.hi[axis]+=distance;
    else span.lo[axis]+=distance;
    const auto lo=floorCell(span.lo),hi=floorCell(span.hi);
    const double margin=std::clamp(size*0.002,1e-10,1e-7);
    double available=matter.sweep(feet,size,axis,distance,height);
    for(int x=lo.x;x<=hi.x;x++)for(int y=lo.y;y<=hi.y;y++)for(int z=lo.z;z<=hi.z;z++){
        const Cell cell{x,y,z};
        const int id=raw(cell);
        if(!furniture(id))continue;
        const auto obstacle=expedition::furnitureVolume(furnitureBase(id),cell.v(),id!=furnitureBase(id));
        if(!span.intersects(obstacle))continue;
        if(distance>0)available=std::min(available,std::max(0.0,obstacle.lo[axis]-body.hi[axis]-margin));
        else available=std::max(available,std::min(0.0,obstacle.hi[axis]-body.lo[axis]+margin));
    }
    return available;
}
std::optional<Hit> Area::ray(Vec start,Vec dir,double reach,int detail) const {
    const auto hitTerrain=matter.ray(start,dir,detail,reach);
    if(hitTerrain)reach=hitTerrain->distance;
    if(auto prop=expedition::furnitureRay(start,dir,reach,[this](Cell cell){return raw(cell);}))return prop;
    return hitTerrain;
}

void Area::drawMeshes(TerrainMeshes& geometry,Bounds portion) const {
    auto& [near,closeAtoms,far,fixtures,fixtureProxy,exact,roof,roofProxy,trees,sprigs]=geometry;
    TerrainFaces faces(portion);
    const auto tree=[&](Cell p){
        if(!inhabited || level!=0 || !within(p) || (p.x>=25 && p.x<=45 && p.z>=10 && p.z<=24) || matter.altered(p) || matter.editedChildren(p))return int(Air);
        const int id=raw(p);return arbor::treeMaterial(id)?id:int(Air);
    };
    if(inhabited && level==0){
        arbor::mesh(trees,sprigs,portion,tree,true);
    }
    for(int z=portion.lo.z;z<portion.hi.z;z++)for(int y=portion.lo.y;y<portion.hi.y;y++)
        for(int x=portion.lo.x;x<portion.hi.x;x++){
        const Cell p{x,y,z};
        const int id=at(p);
        if(!id || tree(p))continue;
        const auto hash=CellHash{}({x+family*78,y+level*8,z});
        const float tone=0.83f+float(hash&63)*0.0034f;
        const Color tint={tone,tone,tone};
        const Vec origin=p.v();
        const bool hold=inhabited && x>=30 && x<=46 && z>=12 && z<=43 && y<=6;
        const bool atoms=level==6 && y>=5 && id!=Bedrock && !hold;
        if(furniture(id)){
            const int design=furnitureBase(id);
            const bool folded=id!=design;
            expedition::decoration(fixtures,design,origin,folded);
            const auto hull=expedition::furnitureVolume(design,origin,folded);
            if(design==Lantern)fixtureProxy.box(hull.lo,hull.hi,Lamp,{0.90f,0.78f,0.55f});
            else if(design==Door || design==Seat)fixtureProxy.box(hull.lo,hull.hi,Timber,tint);
            else{
                const double slope=folded?0.78:0;
                const Vec low=origin+Vec{0,0.14,0},rise{1,slope,0},span{0,0,1};
                fixtureProxy.quad(low,low+span,low+span+rise,low+rise,design==Plank?int(Timber):int(RoofingTexture),tint);
            }
            continue;
        }
        if(level==expedition::Village && id==Clay && (y==8 || y==9) && !edits.contains(p) && !matter.editedChildren(p)){
            const int cx=x<24?10:37;
            const int cz=z<20?14:25;
            if(std::abs(x-cx)<=5 && std::abs(z-cz)<=3 && y==8+(3-std::abs(z-cz))/2){
                expedition::villageRoof(roof,origin,cz+0.5,true);
                expedition::villageRoof(roofProxy,origin,cz+0.5,false);
                continue;
            }
        }
        if(const auto* profile=matter.leafRoof({p,0,id});profile && !matter.altered(p)){
            const auto anchor=portion.lo.v();
            if(profile->low()>=y && profile->high()<=y+1)
                terrain::roofTop(exact,*profile,{double(x)-anchor.x,0,double(z)-anchor.z},id,tint);
            else terrain::roofFragment(exact,*profile,{p,0,id},anchor,tint);
            for(int face:{0,1,4,5}){
                const Cell next=p+Sides[face];
                if(matter.altered(next)){
                    terrain::immediateBoundary(exact,matter,{p,0,id},face,anchor,tint);
                    continue;
                }
                const auto* neighbor=matter.treeThroughRoof(next)?nullptr:matter.roof(next);
                if(!neighbor && matter.sample(0,next).solid())continue;
                terrain::roofWall(exact,*profile,neighbor,p,anchor,face,id,tint);
            }
            continue;
        }
        const auto* naturalRoof=matter.roof(p);
        if(matter.editedChildren(p) || (naturalRoof && naturalRoof->covers(y) && matter.altered(p))){
            terrain::editedRoot(exact,matter,p,portion.lo.v(),tint,!inhabited);
            continue;
        }
        if(const auto piece=matter.shapedPiece({p,0,id})){
            terrain::stone(exact,origin-portion.lo.v(),1.0,id,tint,*piece);
            continue;
        }
        const bool shape=level!=6 && (terrain::mineral(id) || terrain::organic(id));
        const bool bevel=shape && (!inhabited || id!=Grass) && terrain::bevelCell(hash);
        const unsigned air=(bevel || (shape && terrain::mineral(id)))?terrain::exposed([&](Cell d){return matter.sample(0,p+d).amount;}):0;
        const bool onFloor=terrain::mineral(id) && terrain::onFloor(air) && matter.sample(0,p+Sides[2]).amount==FullVolume;
        if(!inhabited && (air==63 || onFloor)){
            const bool persistent=terrain::mineral(id);
            terrain::stone(persistent?exact:near,persistent?origin-portion.lo.v():origin,1.0,id,tint,hash,onFloor);
            continue;
        }
        bool exposed=false;
        for(int face=0;face<6;face++){
            const auto q=p+Sides[face];
            if(matter.editedChildren(q)){
                terrain::immediateBoundary(near,matter,{p,0,id},face,{},tint);
                continue;
            }
            if(within(q)){
                const auto adjacent=matter.sample(0,q);
                if(adjacent.amount && (adjacent.id!=Water || id==Water)){
                    if(matter.leafRoof({q,0,adjacent.id}))
                        terrain::immediateBoundary(near,matter,{p,0,id},face,{},tint);
                    continue;
                }
            }
            exposed=true;
            if(atoms)continue;
            if(bevel && terrain::beveled(air,face))
                terrain::face(near,origin,1,face,id,tint,hash,air);
            else faces.add(p,face,id);
        }
        if(atoms){
            const int material=id==Quartz?SampleIron:SampleCopper;
            closeAtoms.stone(origin+Vec{0.5,0.50,0.50},0.455,material,tint,5,9);
            far.stone(origin+Vec{0.5,0.50,0.50},0.445,material,tint,3,5);
        }else if(level==0 && y<7 && id==Grass && exposed && hash%9==0 && !at(p+Cell{0,1,0})){
            const Vec a=origin+Vec{0.4,1,0.45};
            fixtures.tube(a,a+Vec{0.03,0.54,0.13},0.042,Grass,{0.77f,1,0.63f},5);
            fixtures.stone(a+Vec{0.03,0.62,0.12},0.11,Clay,{1,0.93f,0.8f},3,6);
            fixtureProxy.triangle(a+Vec{-0.07,0,0},a+Vec{0.07,0,0},a+Vec{0.03,0.64,0.12},Grass,{0.77f,1,0.63f});
        }
    }
    faces.draw(near,{family*78,level*8,0});
}

void backdropFace(Geometry& g,Vec origin,double size,int side,int material,Color shade){
    const Box room{{-8,-6,-8},{Width+8.,Height+10.,Depth+8.}};
    const int n=side/2,u=(n+1)%3,v=(n+2)%3;
    Vec a=origin;if(side%2)a[n]+=size;
    const auto emit=[&](double u0,double u1,double v0,double v1){
        if(u1-u0<1e-7 || v1-v0<1e-7)return;
        Vec p=a,q=a,r=a,t=a;p[u]=t[u]=u0;q[u]=r[u]=u1;p[v]=q[v]=v0;r[v]=t[v]=v1;
        if(side%2)g.quad(p,q,r,t,material,shade,float(1/size));
        else g.quad(p,t,r,q,material,shade,float(1/size));
    };
    const double l=a[u],r=l+size,b=a[v],t=b+size;
    const double x0=std::max(l,room.lo[u]),x1=std::min(r,room.hi[u]);
    const double y0=std::max(b,room.lo[v]),y1=std::min(t,room.hi[v]);
    if(a[n]<=room.lo[n] || a[n]>=room.hi[n] || x0>=x1 || y0>=y1){emit(l,r,b,t);return;}
    emit(l,x0,b,t);emit(x1,r,b,t);emit(x0,x1,b,y0);emit(x0,x1,y1,t);
}

void gate(Geometry& g,bool final) {
    const Color stone{.24f,.22f,.25f},metal{.36f,.27f,.20f},edge{.58f,.42f,.28f};
    const Color energy=final?Color{.90f,.48f,.17f}:Color{.12f,.89f,.80f};
    const Color rune{.49f,.065f,.84f};
    constexpr int count=64;
    auto ring=[](double a,double r,double z){return Vec{r*cos(a),.94+r*sin(a)*1.06,z};};
    for(int i=0;i<16;i++){
        double a=2*Pi*i/16,b=a+2*Pi/16;
        double r=.89+.10*double(mix(i+41)&255)/255.;
        Vec p{r*cos(a),-.20,r*.74*sin(a)},q{r*cos(b),-.20,r*.74*sin(b)};
        Vec u=p+Vec{0,.27,0},v=q+Vec{0,.27,0};
        g.quad(p,q,v,u,GateStoneTexture,stone);g.triangle({0,.055,0},v,u,GateStoneTexture,stone);
        Vec root{.70*cos(a),.15,.40*sin(a)},mid{1.02*cos(a),.015,.73*sin(a)},end{1.19*cos(a),-.38,.91*sin(a)};
        g.tube(root,mid,.068,GateStoneTexture,stone,8);g.tube(mid,end,.046,GateStoneTexture,metal,7);
    }
    for(int i=0;i<count;i++){
        double a=2*Pi*i/count,b=2*Pi*(i+1)/count;
        for(double z:{-.115,.115}){
            g.quad(ring(a,.69,z),ring(b,.69,z),ring(b,.78,z),ring(a,.78,z),GateStoneTexture,stone);
            g.quad(ring(a,.78,z),ring(b,.78,z),ring(b,.88,z*.65),ring(a,.88,z*.65),GateStoneTexture,metal);
            g.tube(ring(a,.725,z*1.025),ring(b,.725,z*1.025),.013,GateRuneTexture,energy,6);
        }
        g.quad(ring(a,.88,-.075),ring(b,.88,-.075),ring(b,.88,.075),ring(a,.88,.075),GateStoneTexture,stone);
        g.quad(ring(a,.69,-.115),ring(b,.69,-.115),ring(b,.69,.115),ring(a,.69,.115),GateStoneTexture,edge);
        if(i%4==0){for(double z:{-.134,.134}){
            g.tube(ring(a,.78,z),ring(a+.025,.85,z),.009,GateRuneTexture,rune,5);
            g.tube(ring(a,.815,z),ring(a+.055,.815,z),.009,GateRuneTexture,rune,5);
            g.stone(ring(a,.875,z*.5),.032,GateStoneTexture,edge,4,7);
        }}
        const size_t begin=g.points.size();
        g.triangle({0,.94,0},ring(a,.688,0),ring(b,.688,0),GateEnergyTexture,energy);
        for(size_t j=begin;j<g.points.size();j++){
            auto& v=g.points[j];v.u=v.x/.688f;v.v=(v.y-.94f)/(.688f*1.06f);
        }
    }
    for(double sign:{-1.,1.}){
        g.tube({sign*.67,-.25,0},{sign*.67,.52,0},.11,GateStoneTexture,stone,10);
        for(int k=0;k<3;k++){
            const double x=sign*(.91+.10*k),z=(k-1)*.30;
            g.tube({x,-.20,z},{x+sign*.04,.22+.11*(k==1),z},.055,GateStoneTexture,stone,6);
            g.stone({x+sign*.04,.24+.11*(k==1),z},.041,GateRuneTexture,rune,5,8);
        }
    }
    for(double r:{.51,.75})for(int i=0;i<48;i++){
        double a=2*Pi*i/48,b=2*Pi*(i+1)/48;
        g.tube({r*cos(a),.078,r*.72*sin(a)},{r*cos(b),.078,r*.72*sin(b)},.009,GateRuneTexture,rune,5);
    }
}
}
