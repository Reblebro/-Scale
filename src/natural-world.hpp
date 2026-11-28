#pragma once
#include "math.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

// Continuous, seed-stable terrain. ALL visible roof corners are sampled from
// the same world-space function, including at streamed chunk boundaries.
namespace natural {
inline std::uint32_t hash(int x,int z,std::uint64_t seed,std::uint32_t salt){
    std::uint32_t v=std::uint32_t(x)*0x9e3779b9u ^ std::uint32_t(z)*0x85ebca6bu ^
        std::uint32_t(seed) ^ std::uint32_t(seed>>32)*0xc2b2ae35u ^ salt;
    v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;return v^(v>>16);
}
inline double smooth(double x){x=std::clamp(x,0.0,1.0);return x*x*(3-2*x);}
inline double noise(double x,double z,double scale,std::uint64_t seed,int salt){
    x/=scale;z/=scale;
    const int ix=int(std::floor(x)),iz=int(std::floor(z));
    const double u=smooth(x-ix),v=smooth(z-iz);
    const auto h=[&](int xx,int zz){return double(hash(xx,zz,seed,std::uint32_t(salt))&65535)/65535.0;};
    const double a=std::lerp(h(ix,iz),h(ix+1,iz),u);
    const double b=std::lerp(h(ix,iz+1),h(ix+1,iz+1),u);
    return std::lerp(a,b,v);
}
inline double ridge(double x,double z,double scale,std::uint64_t seed,int salt){
    return 1.0-std::abs(2.0*noise(x,z,scale,seed,salt)-1.0);
}
// Lowlands are broad, walkable regions separated by occasional mountain belts.
// The macro mountain mask is much coarser than individual hills, so hills don't
// tile the ground every 15-30 meters. Nearby detail remains deliberately subtle.
inline double frontierHeight(double x,double z,std::uint64_t seed){
    // Warp the larger ridges so their slopes branch and curve rather than
    // forming repeated smooth, parallel hills. Keep the broad valley floor.
    const double wx=x+32.0*(noise(x,z,125,seed,101)-.5);
    const double wz=z+32.0*(noise(x,z,125,seed,103)-.5);
    const double lowland=28.6+3.3*(noise(x,z,180,seed,11)-.5)+
        2.1*(noise(wx,wz,65,seed,14)-.5)+.95*(noise(wx,wz,18,seed,29)-.5);
    const double mountainRegion=noise(x+117.0,z-87.0,195,seed,53);
    const double mountainMask=std::pow(smooth((mountainRegion-.43)/.24),1.35);
    const double crest=0.24+0.76*ridge(wx-43.0,wz+16.0,76,seed,59);
    const double shoulder= ridge(wx+23,wz-42,39,seed,61);
    const double ravine=std::pow(ridge(wx-64,wz+12,31,seed,71),3.0);
    const double peaks=9.0+19.0*crest+5.0*(shoulder-.5)-4.0*ravine+
        3.5*(noise(wx,wz,18,seed,67)-.5)+1.2*(noise(wx,wz,7,seed,73)-.5);
    const double fromSpawn=std::hypot(x-.5,z+7.0);
    const double mountainDistance=smooth((fromSpawn-53.0)/47.0);
    const double basinRegion=noise(x-173.0,z+91.0,215,seed,83);
    const double basin=smooth((basinRegion-.56)/.22)*
        (1.0-mountainMask)*smooth((fromSpawn-58.0)/42.0);
    return std::clamp(lowland+mountainDistance*mountainMask*peaks-10.0*basin,18.5,60.0);
}
// Keep the first ~14m extremely walkable, then blend seamlessly into rolling
// valley ground. Center exactly at 29 for the existing player and device spawns.
inline double starterHeight(double x,double z,std::uint64_t seed){
    const double centerX=0.5,centerZ=-7.0;
    const double small=noise(x,z,16,seed,391)-noise(centerX,centerZ,16,seed,391);
    const double detail=noise(x,z,5.8,seed,397)-noise(centerX,centerZ,5.8,seed,397);
    return 29.0+0.28*small+0.12*detail;
}
inline double surfaceHeight(double x,double z,std::uint64_t seed){
    const double fromSpawn=std::hypot(x-0.5,z+7.0);
    const double emerge=smooth((fromSpawn-14.0)/31.0);
    return std::lerp(starterHeight(x,z,seed),frontierHeight(x,z,seed),emerge);
}
// Use the SAME height function everywhere, rather than blending the authored
// world with the new terrain in a narrow ring that leaves the camp/quarry intact.
inline bool starterArea(int x,int z){return std::hypot(x-0.5,z+7.0)<45.0;}
// Kept for compatibility with earlier generated source consumers.
inline double starterFade(double x,double z){
    return 1.0-smooth((std::hypot(x-0.5,z+7.0)-14.0)/31.0);
}
// Ore appears as embedded clusters underground, never as material placed on
// top+1 (which previously produced suspended resource clumps).
inline std::uint32_t oreCluster(int x,int y,int z,std::uint64_t seed){
    const int bx=int(std::floor(double(x)/5.0));
    const int by=int(std::floor(double(y)/4.0));
    const int bz=int(std::floor(double(z)/5.0));
    return hash(bx+by*17,bz-by*13,seed,0x7613u);
}

// Trees occupy the existing editable voxel material field. A deterministic
// coarse planting grid gives generous trunk spacing, while crowns spread into
// a high connected canopy in mountain forest belts. Roots/branches are matter,
// not non-interactive billboards; the same tree exists on both sides of a tile.
struct ForestTree {
    struct Crown {double x,z,y,rx,rz,ry;};
    int x{},z{},base{},height{};
    double radius{};
    bool elder{},village{};
    std::uint32_t key{};
    std::vector<Crown> boughs=std::vector<Crown>(12);
    struct Part {
        Vec a,b,radii,lo,hi;
        double r0{},r1{},inverseLength{};
        int material{};
        bool contains(Vec p) const {
            if(p.x<lo.x || p.x>hi.x || p.y<lo.y || p.y>hi.y || p.z<lo.z || p.z>hi.z)return false;
            const Vec d=p-a;
            if(material==1){
                const Vec q{d.x/radii.x,d.y/radii.y,d.z/radii.z};
                return dot(q,q)<=1.0;
            }
            const double f=std::clamp(dot(d,b-a)*inverseLength,0.0,1.0);
            const Vec q=d-(b-a)*f;
            const double r=std::lerp(r0,r1,f);
            return dot(q,q)<=r*r;
        }
    };
    std::vector<Part> parts;
    double stemRadius{},leanX{},leanZ{},bendX{},bendZ{};
    int form{},age{};
    struct Root {double cs,sn,reach,width,curve;};
    std::vector<Root> roots;
};

inline double forestStrength(double x,double z,std::uint64_t seed){
    const double forest=0.71*noise(x+91,z-37,148,seed,1103)+
        0.29*noise(x-133,z+76,71,seed,1109);
    const double hillside=smooth((surfaceHeight(x,z,seed)-29.5)/14.0);
    // Highland woods persist on slopes instead of ending at a fixed altitude.
    return smooth((forest+0.23*hillside-0.34)/0.32);
}
// A tree is a hierarchy, not a cylinder with a sphere on top. Staggered
// lateral crowns, older buttressed stems and long supporting boughs give each
// silhouette several distinct depths. All positions are seed-stable.
inline ForestTree makeTree(int x,int z,int base,int height,double radius,
                           std::uint32_t key,bool elder,bool village){
    ForestTree tree{};
    tree.x=x;tree.z=z;tree.base=base;tree.height=height;tree.radius=radius;
    tree.elder=elder;tree.village=village;tree.key=key;
    const auto rnd=[&](unsigned n){return double(mix(key+n*0x9e3779b9u)&65535)/65535.;};
    if(!village){
        tree.age=elder?2:height<27?0:1;
        tree.form=int(mix(key^0x6917u)%4);
        const double leanAngle=2*Pi*rnd(2),lean=(.65+3.7*rnd(3))*(tree.form==1?1.15:.85);
        tree.leanX=std::cos(leanAngle)*lean;tree.leanZ=std::sin(leanAngle)*lean;
        tree.bendX=(rnd(4)-.5)*3.2;tree.bendZ=(rnd(5)-.5)*3.2;
        tree.stemRadius=elder?2.6+.65*rnd(6):tree.age==0?1.26+.42*rnd(6):1.55+.86*rnd(6);
        const int count=tree.age==0?6+int(rnd(7)*3):elder?11+int(rnd(7)*4):10+int(rnd(7)*5);
        tree.boughs.resize(count);
        // Uneven angular increments, different crown shapes, and age-based
        // branch counts replace the identical twelve-lobe spiral.
        const double bias=2*Pi*rnd(8);
        double angle=2*Pi*rnd(9);
        for(int i=0;i<count;++i){
            const unsigned n=30+unsigned(i)*9;
            angle+=1.25+2.3*rnd(n);
            const double t=(i+.20+.55*rnd(n+1))/count;
            const double low=tree.form==0?.40:tree.form==1?.52:tree.form==2?.33:.47;
            const double tier=std::clamp(low+(1-low)*t+(rnd(n+2)-.5)*.12,.30,.94);
            const double spread=tree.form==1?.96:tree.form==2?1.22:1.10;
            const double outward=radius*spread*(.66+.42*rnd(n+3))*(1-.33*t)*(.91+.17*std::cos(angle-bias));
            const double width=radius*(.46+.18*rnd(n+4))*(tree.form==1?.90:1.0);
            const double y=base+height*tier;
            const double ry=std::clamp(width*(.55+.43*rnd(n+5)),1.65,5.0);
            tree.boughs[i]={std::cos(angle)*outward,std::sin(angle)*outward,y,
                width*(.85+.27*rnd(n+6)),width*(.78+.35*rnd(n+7)),ry};
        }
        const int roots=4+int(rnd(10)*5);
        double angleRoot=2*Pi*rnd(11);
        for(int i=0;i<roots;++i){
            angleRoot+=2*Pi/roots*(.70+.60*rnd(200+i*4));
            tree.roots.push_back({std::cos(angleRoot),std::sin(angleRoot),
                (tree.age==0?5.2:elder?8.0:6.1)*(.78+.44*rnd(201+i*4)),
                std::max(.92,tree.stemRadius*.53)*(.85+.36*rnd(202+i*4)),
                (rnd(203+i*4)-.5)*1.5});
        }
        return tree;
    }
    constexpr double tau=6.283185307179586;
    const double rotation=tau*double((key>>6)&255)/256.0;
    const int habit=int((key>>19)%3);
    const double levels=double(tree.boughs.size()-1);
    for(int i=0;i<int(tree.boughs.size());++i){
        const auto variant=hash(x+i*31,z-i*37,key,0x12f4u);
        const double angle=rotation+i*2.399963229728653;
        const double random=double(variant&255)/255.0;
        const double drift=double((variant>>8)&255)/255.0-0.5;
        const double tier=double(i)/levels;
        const double spread=habit==0?1.16:habit==1?0.88:1.34;
        const double taper=1.0-0.30*tier;
        const double orbit=radius*spread*(0.74+0.30*random)*taper;
        const double droop=habit==1?.42:.36;
        const double centerY=base+height-1.8-(1.0-tier)*height*droop+drift*1.3;
        const double size=radius*(habit==1?.40:.46)*(0.95+0.34*random)*(1.0-0.09*tier);
        const double ry=(habit==1?2.95:2.30)+double((variant>>24)&15)*0.08+(tier<.35?1.35:0.0);
        tree.boughs[i]={std::cos(angle)*orbit,std::sin(angle)*orbit,centerY,
            size*(0.92+double((variant>>16)&15)/42.0),
            size*(0.92+double((variant>>20)&15)/42.0),ry};
    }
    return tree;
}
// Sparse, wandering trail corridors. Both tree placement and lantern placement
// use these world-space coordinates, independent of the tile load order.
inline constexpr int TrailInterval=8, TrailLane=96, TreeSpacing=12, TreeReach=36;
inline constexpr double TrailLightRadius=6.4;
inline double trailX(int lane,double z,std::uint64_t seed){
    const double center=lane*double(TrailLane)+18.0;
    return center+22.0*(noise(center,z,64,seed,0x71a1)-.5)
        +10.0*(noise(center,z,31,seed,0x71a2)-.5);
}
inline int trailLane(double x){return int(std::floor((x-18.0)/TrailLane+.5));}
inline bool trailLand(int lane,double z,std::uint64_t seed){
    const double x=trailX(lane,z,seed);
    const double h=surfaceHeight(x,z,seed);
    if(h<25.0 || h>59.0)return false;
    if(std::hypot(x-.5,z+7.0)<12.0)return false;
    const double prev=surfaceHeight(trailX(lane,z-1,seed),z-1,seed);
    const double next=surfaceHeight(trailX(lane,z+1,seed),z+1,seed);
    return std::max(std::abs(h-prev),std::abs(next-h))<.65 &&
        std::abs(surfaceHeight(x-2,z,seed)-surfaceHeight(x+2,z,seed))<2.0;
}
inline double trailHalfWidth(double x,double z,std::uint64_t seed){
    return 1.40+.33*noise(x,z,9,seed,0x7a19);
}
// Signed horizontal distance to the path edge. The mesh interpolates this
// continuous field across roof triangles; the actual soil remains mineable.
inline double trailEdge(double x,double z,std::uint64_t seed){
    const int lane=trailLane(x);
    const double distance=trailHalfWidth(x,z,seed)-std::abs(x-trailX(lane,z,seed));
    if(distance<-2.0 || !trailLand(lane,z,seed))return -2.0;
    return std::clamp(distance,-2.0,2.0);
}
inline bool trailSurface(double x,double z,std::uint64_t seed){
    const int lane=trailLane(x);
    return std::abs(x-trailX(lane,z,seed))<trailHalfWidth(x,z,seed) && trailLand(lane,z,seed);
}
inline bool trailReserved(double x,double z,std::uint64_t seed){
    const int lane=trailLane(x);
    // Protect the winding corridor, but do not deforest impassable terrain.
    return std::abs(x-trailX(lane,z,seed))<5.8 && trailLand(lane,z,seed);
}
inline std::vector<Vec> trailLanterns(int tx,int tz,std::uint64_t seed){
    std::vector<Vec> lights;
    const int minX=tx*32,minZ=tz*32;
    for(int lane=trailLane(minX)-1;lane<=trailLane(minX+31)+1;++lane){
        const auto key=hash(lane,0,seed,0x1a47u);
        const int offset=int(key%TrailInterval);
        for(int row=divFloor(minZ-offset,TrailInterval);row<=divFloor(minZ+31-offset,TrailInterval);++row){
            const int z=row*TrailInterval+offset;
            if(z<minZ || z>=minZ+32 || !trailLand(lane,z,seed))continue;
            // One post per station, alternating sides even across negative rows.
            const double side=((row+(key&1))%2!=0?1.0:-1.0)*2.15;
            const double x=trailX(lane,z,seed)+side;
            if(x<minX || x>=minX+32)continue;
            // The post sits to one side of the walking line, not in its center.
            const double h=surfaceHeight(x,z,seed);
            if(h<25 || h>59 || std::abs(h-surfaceHeight(trailX(lane,z,seed),z,seed))>1.25)continue;
            lights.push_back({x,h,double(z)});
        }
    }
    return lights;
}
// A curved axis follows the tree's own growth history, not a rigid cylinder.
inline Vec stemAt(const ForestTree& t,double y){
    const double f=std::clamp((y-t.base)/t.height,0.,1.);
    const double bend=std::sin(Pi*f)*f;
    return {t.leanX*f*f+t.bendX*bend,y,t.leanZ*f*f+t.bendZ*bend};
}
inline void woodPart(ForestTree& t,Vec a,Vec b,double r0,double r1){
    const double r=std::max(r0,r1);const Vec pad{r,r,r};
    const Vec d=b-a;
    t.parts.push_back({a,b,{},Vec{std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z)}-pad,
        Vec{std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)}+pad,
        r0,r1,1/std::max(.0001,dot(d,d)),2});
}
inline void leafPart(ForestTree& t,Vec a,Vec radii){
    t.parts.push_back({a,{},radii,a-radii,a+radii,0,0,0,1});
}
inline void prepareTree(ForestTree& t){
    if(t.village)return;
    t.parts.clear();t.parts.reserve(120);
    const auto rnd=[&](unsigned n){return double(mix(t.key+n*0x632be59bu)&65535)/65535.;};
    const auto r=[&](double f){return std::max(1.14,t.stemRadius*(1-.67*f)+.78*std::exp(-f*t.height*.39));};
    const int levels=std::max(5,t.height/4);
    for(int i=0;i<levels;++i){
        const double a=double(i)/levels,b=double(i+1)/levels;
        woodPart(t,stemAt(t,t.base+t.height*a),stemAt(t,t.base+t.height*b),r(a),r(b));
    }
    // Each primary branch supports secondary branches and uneven leaf fans.
    // Branch tips stay inside the leaf mass, including after competition.
    for(unsigned i=0;i<t.boughs.size();++i){
        const auto& c=t.boughs[i];const unsigned n=100+i*17;
        const double at=std::max(t.base+4.0,c.y-t.height*(.12+.12*rnd(n)));
        const Vec start=stemAt(t,at),end{c.x,c.y,c.z};
        const Vec delta=end-start;
        const Vec side=unit(Vec{-delta.z,0,delta.x});
        const Vec elbow=start+delta*.49+side*((rnd(n+1)-.5)*2.4)+Vec{0,-.6-1.2*rnd(n+2),0};
        const double thickness=std::max(1.36,t.stemRadius*(.62+.18*rnd(n+3)));
        woodPart(t,start,elbow,thickness,1.36);
        woodPart(t,elbow,end,1.36,1.20);
        if(t.age==2 && rnd(n+4)<.065)continue; // weathered bare branch
        leafPart(t,end,{c.rx,c.ry,c.rz});
        const int fans=2+(rnd(n+5)>.64);
        const Vec outward=unit(Vec{delta.x,0,delta.z});
        for(int j=0;j<fans;++j){
            const unsigned k=n+6+j*3;
            const double sign=j%2?1.:-1.;
            const Vec tip=end+side*(sign*c.rx*(.38+.24*rnd(k)))+
                outward*(c.rz*(.12+.22*rnd(k+1)))+Vec{0,(rnd(k+2)-.5)*c.ry*.75,0};
            woodPart(t,elbow+(end-elbow)*.76,tip,1.27,1.18);
            leafPart(t,tip,{c.rx*(.55+.17*rnd(k+1)),std::max(1.50,c.ry*.69),c.rz*(.51+.20*rnd(k))});
        }
    }
    const Vec top=stemAt(t,t.base+t.height-1.9);
    const double crown=t.radius*(t.form==1?.44:.50);
    leafPart(t,top,{crown,3.3,crown*(.83+.24*rnd(10))});
    if(t.form==3){ // forked dominant stem
        const Vec a=stemAt(t,t.base+t.height*.55);
        const double angle=2*Pi*rnd(11);
        const Vec b=top+Vec{std::cos(angle)*t.radius*.57,-3.2,std::sin(angle)*t.radius*.57};
        woodPart(t,a,a+(b-a)*.56,1.65,1.36);
        woodPart(t,a+(b-a)*.56,b,1.36,1.20);
        leafPart(t,b,{crown*.86,3.0,crown*.93});
    }
}
inline std::optional<ForestTree> rawTreeAtGrid(int cx,int cz,std::uint64_t seed){
    constexpr int spacing=TreeSpacing;
    const auto key=hash(cx,cz,seed,0xf025u);
    const int x=cx*spacing+int((key>>4)%spacing);
    const int z=cz*spacing+int((key>>16)%spacing);
    const double fromSpawn=std::hypot(x-0.5,z+7.0);
    if(fromSpawn<35.0)return {};
    const double strength=forestStrength(x,z,seed);
    const double grove=noise(x,z,41,seed,0xc417u);
    if(key%1000>=unsigned(390+int(535*strength)+int(74*grove)))return {};
    if(trailReserved(x+.5,z+.5,seed))return {};
    const double root=surfaceHeight(x+0.5,z+0.5,seed);
    if(root<24.5 || root>59.0)return {};
    const double slope=std::max(
        std::abs(surfaceHeight(x+1.5,z+0.5,seed)-surfaceHeight(x-0.5,z+0.5,seed)),
        std::abs(surfaceHeight(x+0.5,z+1.5,seed)-surfaceHeight(x+0.5,z-0.5,seed)));
    if(slope>2.50)return {};
    const bool elder=(key>>9)%9==0 && root<43;
    const bool young=!elder && root<42 && (mix(key^0x724au)%100)<12;
    const int base=int(std::floor(root));
    // Size is relative to the ground, not an absolute canopy elevation.
    // A 96-cell runtime column has room for mature trees on high slopes.
    const int wanted=elder?39+int((key>>23)%9):young?19+int((key>>23)%7):27+int((key>>23)%13);
    const int height=std::min(wanted,ForestWorldHeight-7-base);
    if(height<(young?19:25))return {};
    const double radius=(elder?11.4+double(key&3)*.60:8.2+double(key&7)*.42)*
        std::clamp(height/32.0,.62,1.10)*(young?.84:1.0);
    return makeTree(x,z,base,height,radius,key,elder,false);
}
// Growth competition uses the unmodified neighbors, so either tile computes
// identical crowns. Trees retain their wood and height; crowded lateral fans
// shrink modestly while exposed fans can expand into the available space.
inline std::optional<ForestTree> treeAtGrid(int cx,int cz,std::uint64_t seed){
    auto tree=rawTreeAtGrid(cx,cz,seed);
    if(!tree)return {};
    std::vector<ForestTree> neighbors;
    for(int dz=-2;dz<=2;++dz)for(int dx=-2;dx<=2;++dx){
        if(!dx && !dz)continue;
        if(auto other=rawTreeAtGrid(cx+dx,cz+dz,seed)){
            // Deterministic thinning: irregular sites, but no colliding boles.
            const double distance=std::hypot(tree->x-other->x,tree->z-other->z);
            if(distance<8.0 && (other->key<tree->key || (other->key==tree->key &&
                std::pair{other->x,other->z}<std::pair{tree->x,tree->z})))return {};
            neighbors.push_back(std::move(*other));
        }
    }
    for(auto& crown:tree->boughs){
        double crowd=0;
        for(const auto& other:neighbors){
            // Compare actual crown centers in 3D: vertically separated crowns
            // don't compete just because their projected outlines intersect.
            for(const auto& near:other.boughs){
                const double d=std::hypot(tree->x+crown.x-other.x-near.x,
                                          tree->z+crown.z-other.z-near.z);
                const double y=std::abs(crown.y-near.y);
                const double horizontal=smooth(1-d/std::max(1.0,(crown.rx+near.rx)*1.45));
                const double vertical=smooth(1-y/std::max(1.0,crown.ry+near.ry));
                crowd=std::max(crowd,horizontal*vertical);
            }
        }
        const double growth=1.10-.21*crowd;
        crown.rx*=growth;crown.rz*=growth;
    }
    prepareTree(*tree);
    return tree;
}
// Landmark sites are much rarer than regular trees. These are canopy hamlets,
// not yet separate recursively scalable world instances. The site/seed is
// explicit so a later scale-transition system can attach a nested world here.
inline std::optional<ForestTree> elfVillageAtSector(int sx,int sz,std::uint64_t seed){
    constexpr int extent=192;
    const auto key=hash(sx,sz,seed,0xe1f5u);
    if(key%2!=0)return {};
    const int x=sx*extent+49+int((key>>7)%95);
    const int z=sz*extent+49+int((key>>18)%95);
    if(std::hypot(x-0.5,z+7.0)<95 || forestStrength(x,z,seed)<0.35)return {};
    const double height=surfaceHeight(x+0.5,z+0.5,seed);
    if(height<27 || height>38)return {};
    const double slope=std::max(
        std::abs(surfaceHeight(x+1.5,z+0.5,seed)-surfaceHeight(x-0.5,z+0.5,seed)),
        std::abs(surfaceHeight(x+0.5,z+1.5,seed)-surfaceHeight(x+0.5,z-0.5,seed)));
    if(slope>0.95)return {};
    const int base=int(std::floor(height));
    return makeTree(x,z,base,std::min(24,58-base),9.8,key,true,true);
}
inline std::vector<ForestTree> treesNearTile(int tx,int tz,std::uint64_t seed){
    constexpr int reach=TreeReach;
    const int minX=tx*32,maxX=minX+31,minZ=tz*32,maxZ=minZ+31;
    std::vector<ForestTree> trees;
    for(int cz=divFloor(minZ-reach,TreeSpacing);cz<=divFloor(maxZ+reach,TreeSpacing);++cz)
        for(int cx=divFloor(minX-reach,TreeSpacing);cx<=divFloor(maxX+reach,TreeSpacing);++cx)
            if(auto tree=treeAtGrid(cx,cz,seed))
                if(tree->x>=minX-reach && tree->x<=maxX+reach &&
                   tree->z>=minZ-reach && tree->z<=maxZ+reach)
                    trees.push_back(*tree);
    for(int sz=divFloor(minZ-reach,192);sz<=divFloor(maxZ+reach,192);++sz)
        for(int sx=divFloor(minX-reach,192);sx<=divFloor(maxX+reach,192);++sx)
            if(auto village=elfVillageAtSector(sx,sz,seed))
                if(village->x>=minX-reach && village->x<=maxX+reach &&
                   village->z>=minZ-reach && village->z<=maxZ+reach)
                    trees.push_back(*village);
    return trees;
}
// Return 1 for foliage and 2 for structural timber. Tree trunks have a
// slight lean and spiraling buttresses; branches rise to distinct crown lobes.
// The height axis is bounded by the expanded runtime world.
inline int villageTreeMatter(const ForestTree& t,int x,int y,int z){
    const double dx=double(x-t.x),dz=double(z-t.z),h=y-t.base;
    const double outer=TreeReach;
    if(std::abs(dx)>outer || std::abs(dz)>outer || y<t.base-4 || y>ForestWorldHeight-2)return 0;
    const int habit=int((t.key>>19)%3);
    // The expensive bark profile is only relevant near the bole.
    if(std::abs(dx)<6 && std::abs(dz)<6 && h<t.height-.4){
        const double progress=std::clamp(h/t.height,0.0,1.0);
        const double swayX=(habit==2?1.1:.55)*std::sin(progress*3.8+double(t.key&31)*.13)*progress;
        const double swayZ=.85*std::sin(progress*4.6+double((t.key>>5)&31)*.17)*progress;
        const double px=dx-swayX,pz=dz-swayZ;
        const double angle=std::atan2(pz,px);
        const double rib=.20*std::cos(angle*5+progress*2.0+double(t.key&15))+
                         .09*std::cos(angle*9-progress*1.8);
        const double baseRadius=t.elder?2.8:1.85+double(t.key&7)*.075;
        const double trunk=baseRadius*(1.0-.69*progress)+
            (t.elder?1.5:1.05)*std::exp(-std::max(0.0,h)*.66)+rib;
        if(std::hypot(px,pz)<=std::max(.85,trunk))return 2;
    }
    if(h<t.height*.18)return 0;
    int material=0;
    for(int i=0;i<int(t.boughs.size());++i){
        const auto& l=t.boughs[i];
        const double tier=double(i)/double(t.boughs.size()-1);
        const double startY=l.y-t.height*(.23-.055*tier);
        // Use true three-dimensional distance to the two segments, not
        // vertical distance at a horizontal projection (which broke steep
        // branches into disconnected voxel staircases).
        const Vec start{0,startY,0},end{l.x,l.y,l.z};
        const Vec elbow=start+(end-start)*.48+Vec{0,-.70,0};
        const Vec point{dx,double(y),dz};
        const auto inside=[&](Vec a,Vec b,double r0,double r1){
            const Vec d=b-a;
            const double f=std::clamp(dot(point-a,d)/std::max(.001,dot(d,d)),0.0,1.0);
            const double radius=std::lerp(r0,r1,f);
            const Vec delta=point-(a+d*f);
            return dot(delta,delta)<=radius*radius;
        };
        if(dx>=std::min(0.0,l.x)-2 && dx<=std::max(0.0,l.x)+2 &&
           dz>=std::min(0.0,l.z)-2 && dz<=std::max(0.0,l.z)+2 &&
           y>=startY-3 && y<=l.y+2 &&
           (inside(start,elbow,t.elder?1.95:1.70,1.34) || inside(elbow,end,1.34,1.02)))return 2;
        if(std::abs(y-l.y)>l.ry+1 || std::abs(dx-l.x)>l.rx*1.6+1 || std::abs(dz-l.z)>l.rz*1.6+1)continue;
        const double a=(dx-l.x)/l.rx,b=(dz-l.z)/l.rz,c=(y-l.y)/l.ry;
        const double ripple=.16*std::sin(a*5.3+b*3.7+i)+.12*std::sin(b*6.1-c*4.9+i*1.7);
        if(a*a+b*b+c*c<1.10+ripple)material=1;
        // Two smaller overlapping fans grow off each primary bough. They
        // scallop the outline instead of leaving eight identical round balls.
        const double radial=std::atan2(l.z,l.x);
        for(int fan:{-1,1}){
            const double theta=radial+fan*.90;
            const double ox=std::cos(theta)*l.rx*.67,oz=std::sin(theta)*l.rz*.67;
            const double aa=(dx-l.x-ox)/(l.rx*.74),bb=(dz-l.z-oz)/(l.rz*.74);
            const double cc=(y-l.y-fan*.42)/(l.ry*.92);
            if(aa*aa+bb*bb+cc*cc<1.08+.12*std::sin(aa*5+bb*4+i))material=1;
        }
    }
    const double leafY=t.base+t.height-2.0;
    const double centralRadius=t.radius*(habit==1?.46:.56);
    if((dx*dx+dz*dz)/(centralRadius*centralRadius)+
       (y-leafY)*(y-leafY)/9.8<1.0)material=1;
    return material;
}
inline int treeMatter(const ForestTree& t,int x,int y,int z){
    if(t.village)return villageTreeMatter(t,x,y,z);
    const Vec q{double(x-t.x),double(y),double(z-t.z)};
    if(std::abs(q.x)>TreeReach || std::abs(q.z)>TreeReach || y<t.base-4 || y>ForestWorldHeight-2)return 0;
    int material=0;
    for(const auto& part:t.parts)if(part.contains(q)){
        if(part.material==2)return 2;
        material=1;
    }
    return material;
}
// Clip each prepared part to the tile. The same occupancy function is used
// by the point queries and this fast scan; internal faces remain suppressed.
template<class Write> void rasterTree(const ForestTree& t,Bounds area,Write write){
    for(const auto& part:t.parts){
        const int x0=std::max({area.lo.x,t.x-TreeReach,int(std::ceil(part.lo.x))+t.x});
        const int x1=std::min({area.hi.x-1,t.x+TreeReach,int(std::floor(part.hi.x))+t.x});
        const int z0=std::max({area.lo.z,t.z-TreeReach,int(std::ceil(part.lo.z))+t.z});
        const int z1=std::min({area.hi.z-1,t.z+TreeReach,int(std::floor(part.hi.z))+t.z});
        const int y0=std::max({area.lo.y,t.base-4,int(std::ceil(part.lo.y))});
        const int y1=std::min({area.hi.y-1,ForestWorldHeight-2,int(std::floor(part.hi.y))});
        for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x)for(int y=y0;y<=y1;++y)
            if(part.contains({double(x-t.x),double(y),double(z-t.z)}))write(x,y,z,part.material);
    }
}
// Surface-following root ribs. Their cross sections are centered slightly
// *below* the local roof and taper underground, with only short ridges peeking
// out close to the bole. This avoids hovering stumps and giant surface roots.
inline bool rootMatter(const ForestTree& t,int x,int y,int z,double ground){
    const double dx=double(x-t.x),dz=double(z-t.z);
    if(std::abs(y+0.5-ground)>2.2)return false;
    if(!t.village){
        for(const auto& r:t.roots){
            const double along=dx*r.cs+dz*r.sn;
            if(along<0 || along>r.reach)continue;
            const double f=along/r.reach;
            const double side=-dx*r.sn+dz*r.cs-r.curve*std::sin(Pi*f);
            if(std::abs(side)>r.width*(1-f)+.22)continue;
            const double center=ground+.16*(1-f)-.74*f;
            if(std::abs(y+.5-center)<=1.12*(1-f)+.22)return true;
        }
        return false;
    }
    constexpr double tau=6.283185307179586;
    const double rotation=tau*double((t.key>>5)&255)/256.0;
    for(int i=0;i<6;++i){
        const double a=rotation+i*tau/6.0+0.10*std::sin(i*2.9);
        const double cs=std::cos(a),sn=std::sin(a);
        const double along=dx*cs+dz*sn;
        const double reach=(t.elder?7.8:5.7)+(i%3)*0.68;
        if(along<0 || along>reach)continue;
        const double t01=along/reach;
        const double sideways=-dx*sn+dz*cs-0.26*std::sin(t01*4.2+i);
        const double width=(t.elder?1.33:1.06)*(1.0-t01)+0.18;
        if(std::abs(sideways)>width)continue;
        const double centerY=ground+(i%2==0?.28:-.28)*(1.0-t01)-.78*t01;
        const double thickness=1.05*(1.0-t01)+0.24;
        if(std::abs(y+0.5-centerY)<=thickness)return true;
    }
    return false;
}
// Recognizable, rare elf canopy settlements: tiered wooden balconies, a
// broad main deck, two small roofed dwellings and four lantern posts. The
// central elder trunk is the structural support for the entire settlement.
// Return a material name-code to keep this header independent of world.hpp.
// 0 none, 1 Timber, 2 Plank, 3 RoofTile, 4 Lantern.
inline int villageMatter(const ForestTree& t,int x,int y,int z){
    if(!t.village)return 0;
    const int dx=x-t.x,dz=z-t.z;
    const double r=std::hypot(double(dx),double(dz));
    const int deck=t.base+t.height-10;
    if(r>7 || y<deck-9 || y>deck+5)return 0;
    if(y==deck && r<=6.0)return 2;
    if(y==deck-1 && r<=2.6)return 1;
    if(y==deck+1 && r>=5.2 && r<=5.9 && (std::abs(dx)<4 || std::abs(dz)<4))return 1;
    for(const int level:{deck-8,deck-5,deck-3}){
        // Offset terraces circle the bole in a stepped spiral.
        const int shift=(level+int(t.key&3))%4;
        const bool terrace=shift==0?dx>=0&&dz>=0:shift==1?dx<=0&&dz>=0:
                           shift==2?dx<=0&&dz<=0:dx>=0&&dz<=0;
        if(y==level && r>=1.5 && r<=3.5 && terrace)return 2;
    }
    const std::array<std::array<int,2>,2> huts={{{3,0},{-3,1}}};
    for(auto home:huts){
        const int hx=dx-home[0],hz=dz-home[1];
        if(std::abs(hx)>1 || std::abs(hz)>1)continue;
        if(y==deck+4)return 3;
        if(y>deck && y<deck+4 &&
            (std::abs(hx)==1 || std::abs(hz)==1) && !(hx==0 && hz==-1 && y==deck+1))return 1;
    }
    if(y==deck+2 && ((dx==5 && dz==0)||(dx==-5 && dz==0)||
                        (dz==5 && dx==0)||(dz==-5 && dx==0)))return 4;
    return 0;
}
}
