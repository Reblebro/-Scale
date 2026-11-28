#include "world.hpp"
#include <queue>
#include <unordered_set>

namespace {
constexpr std::array<Cell,4> corners={{{0,0,0},{1,0,0},{1,0,1},{0,0,1}}};
using Heights=std::array<double,4>;

// Integrate the two terrain triangles clipped to the horizontal top and
// bottom of this leaf. Inventory and contact use the same diagonal.
std::int64_t soilUnits(const Heights& h){
    const auto above=[&](double plane){
        std::array<Vec,4> points{};
        for(int i=0;i<4;i++)points[i]=corners[i].v()+Vec{0,h[i]-plane,0};
        double sum=0;
        for(const auto& face:TerrainRoof::faces){
            std::array<Vec,4> clipped{};
            int count=0;
            Vec last=points[face.back()];
            for(int index:face){
                const Vec p=points[index];
                if((p.y>0)!=(last.y>0))clipped[count++]=last+(p-last)*(last.y/(last.y-p.y));
                if(p.y>0)clipped[count++]=p;
                last=p;
            }
            for(int i=2;i<count;i++){
                const auto a=clipped[0],b=clipped[i-1],c=clipped[i];
                sum+=std::abs(cross(b-a,c-a).y)*(a.y+b.y+c.y)/6;
            }
        }
        return sum;
    };
    return std::llround(std::clamp(above(0)-above(1),0.0,1.0)*(FullBlock/64));
}

// Once an isolated faceted rock is edited, its procedural rock mesh no longer
// owns the root. Seed the editable columns from that mesh before cutting it,
// so untouched corners do not suddenly become a solid cube.
void fracture(World& world,Leaf leaf,const TerrainStone& rock){
    const auto top=[&](double x,double z){
        const auto hit=rock.ray({x,2,z},{0,-1,0},0,2.01);
        return hit?2-hit->distance:0.0;
    };
    for(int z=0;z<4;z++)for(int x=0;x<4;x++){
        Heights column;
        for(int i=0;i<4;i++)column[i]=4*top((x+corners[i].x)*0.25,(z+corners[i].z)*0.25);
        const auto bottom=rock.ray({(x+0.5)/4,-1,(z+0.5)/4},{0,1,0},0,2.01);
        for(int y=0;y<4;y++){
            SoilHeights cap;
            Heights h;
            for(int i=0;i<4;i++){
                cap[i]=std::int16_t(std::lround(256*(column[i]-y)));
                h[i]=cap[i]/256.0;
            }
            const bool solid=soilUnits(h)>=FullBlock/2048 &&
                (rock.grounded || (bottom && y+1>(bottom->distance-1)*4));
            Edit edit{leaf.level+1,leaf.position*4+Cell{x,y,z},solid?leaf.material:Air};
            if(solid)edit.surface=cap;
            world.apply(edit);
        }
    }
}

struct StrokeCell {
    Leaf leaf;
    Heights before,ground,offset;
    std::int64_t original;
    bool empty;
};
}

// The cut follows the impact plane. Deposits grow upward from supported terrain.
std::int64_t World::shovel(Vec point,int tier,int material,bool deposit,std::int64_t budget,
                           const std::function<bool(Leaf)>& allowed,Vec normal){
    if(tier<0 || tier>FineDepth || !sculptable(material) || budget<=0)return 0;
    const int level=std::min(tier+1,FineDepth),n=subdivisions(level);
    const Vec center=point*n;
    const double radius=tier==FineDepth?1.25:4*ShovelRadius;
    const double depth=tier==FineDepth?0.85:1.65;
    const int unitScale=tier==FineDepth?64:1;
    const auto heights=[&](Cell cell,const TerrainRoof* roof,double fallback){
        Heights result;
        const Cell root=parent(cell,n);
        for(int i=0;i<4;i++){
            const auto p=(cell+corners[i]).v()/n-root.v();
            result[i]=roof?roof->height(p.x,p.z)*n:fallback;
        }
        return result;
    };
    if(!deposit){
        // Cut one continuous bowl through the intersecting leaves. A point
        // sample at the column center misses thin rims and finer saved edits.
        const double sx=normal.y>0.55?-normal.x/normal.y:0;
        const double sz=normal.y>0.55?-normal.z/normal.y:0;
        const double vertical=depth+radius*(std::abs(sx)+std::abs(sz));
        const Vec ext{radius,vertical,radius};
        const Box bounds{(center-ext)/n,(center+ext)/n};
        if(sculptedTerrain){
            std::vector<std::pair<Leaf,TerrainStone>> rocks;
            visitMatter(bounds,[&](Leaf leaf){
                if(leaf.level>=FineDepth || harvestMaterial(leaf.material)!=material || !allowed(leaf))return true;
                const auto shape=shapedPiece(leaf);
                if(!shape)return true;
                const auto box=leaf.bounds();
                const double width=box.hi.x-box.lo.x;
                const auto contact=shape->occupied({(bounds.lo-box.lo)/width,(bounds.hi-box.lo)/width});
                if(!contact)return true;
                const Vec near{std::clamp(point.x,box.lo.x+contact->lo.x*width,box.lo.x+contact->hi.x*width),0,
                               std::clamp(point.z,box.lo.z+contact->lo.z*width,box.lo.z+contact->hi.z*width)};
                if(std::hypot(point.x-near.x,point.z-near.z)*n<radius)rocks.emplace_back(leaf,*shape);
                return true;
            });
            for(const auto& [leaf,shape]:rocks)fracture(*this,leaf,shape);
        }
        std::vector<Edit> cuts;
        double credit=0;
        const auto cut=[&](Leaf leaf){
            if(harvestMaterial(leaf.material)!=material || !allowed(leaf))return;
            const int fine=subdivisions(leaf.level);
            const double ratio=double(fine)/n;
            const Vec lo=leaf.position.v()/ratio-center;
            const auto* roof=leafRoof(leaf);
            const Cell root=parent(leaf.position,fine);
            if(root.y<1 || root.y>=heightLimit() || root.x< -2048 || root.x>=2048 || root.z< -2048 || root.z>=2048)return;
            const double width=1/ratio;
            const double nx=std::clamp(0.0,lo.x,lo.x+width),nz=std::clamp(0.0,lo.z,lo.z+width);
            if(nx*nx+nz*nz>=radius*radius)return;
            Heights before{},after{};
            SoilHeights packed{};
            for(int i=0;i<4;i++){
                const Vec p=(leaf.position+corners[i]).v()/fine-root.v();
                before[i]=roof?roof->height(p.x,p.z)*fine-leaf.position.y:1.0;
                const double x=lo.x+corners[i].x/ratio,z=lo.z+corners[i].z/ratio;
                const double r2=(x*x+z*z)/(radius*radius);
                const double mid=sx*x+sz*z-lo.y;
                // The upper extent matters when cutting into a bank from the side.
                const bool above=mid+depth*(1-r2)<=0;
                const double floor=(mid+depth*(r2-1))*ratio;
                const double h=above?before[i]:(roof?std::min(before[i],floor):floor);
                packed[i]=std::int16_t(std::lround(std::clamp(h,-100.0,100.0)*256));
                after[i]=packed[i]/256.0;
            }
            const auto prior=soilUnits(before);
            auto left=soilUnits(after);
            // Collapse wafer-thin remainders in the actual edit, including
            // their ray/collision surface. Charge the removed material once.
            if(left<FullBlock/2048)left=0;
            if(left>=prior && (prior || !roof))return;
            const auto id=leaf.material==Grass?int(Dirt):leaf.material;
            Edit edit{leaf.level,leaf.position,left?id:Air};
            if(left)edit.surface=packed;
            cuts.push_back(edit);
            credit+=(prior-left)*unitScale/(ratio*ratio*ratio);
        };
        visitMatter(bounds,[&](Leaf leaf){
            if(harvestMaterial(leaf.material)!=material)return true;
            if(leaf.level>=level){cut(leaf);return true;}
            const int count=subdivisions(level-leaf.level);
            const Cell start=leaf.position*count,last=start+Cell{count-1,count-1,count-1};
            const Cell a=floorCell(bounds.lo*n),b=floorCell(bounds.hi*n);
            for(int z=std::max(a.z,start.z);z<=std::min(b.z,last.z);z++)
                for(int x=std::max(a.x,start.x);x<=std::min(b.x,last.x);x++)
                    for(int y=std::max(a.y,start.y);y<=std::min(b.y,last.y);y++)
                        cut({{x,y,z},level,leaf.material});
            return true;
        });
        for(const auto& edit:cuts)apply(edit);
        return std::llround(credit);
    }
    std::vector<StrokeCell> cells;
    for(int z=int(std::floor(center.z-radius));z<=std::floor(center.z+radius);z++)
        for(int x=int(std::floor(center.x-radius));x<=std::floor(center.x+radius);x++){
            Heights offset{};
            for(int i=0;i<4;i++){
                const double dx=x+corners[i].x-center.x,dz=z+corners[i].z-center.z;
                const double weight=std::max(0.0,1-(dx*dx+dz*dz)/(radius*radius));
                offset[i]=depth*weight*weight;
            }
            if(std::all_of(offset.begin(),offset.end(),[](double v){return v==0;}))continue;
            const Vec probe{(x+0.5)/n,(center.y+2.1)/n,(z+0.5)/n};
            const auto top=ray(probe,{0,-1,0},level,4.3/n);
            if(!top)continue;
            const bool buried=top->distance<1e-9;
            if(buried)continue;
            const double elevation=(probe.y-top->distance)*n;
            const auto* surface=leafRoof({top->cell,top->level,top->id});
            const bool sameSoil=harvestMaterial(top->id)==material;
            // Hard or different material can support a pile on its flat face.
            // Two materials cannot share the same partial leaf.
            if(!sameSoil && (surface || std::abs(elevation-std::round(elevation))>1e-5))continue;
            const auto ground=heights({x,0,z},surface,elevation);
            int low=int(std::floor(*std::min_element(ground.begin(),ground.end())));
            int high=int(std::ceil(*std::max_element(ground.begin(),ground.end())+depth))-1;
            low=std::max(low,int(std::floor(center.y))-5);
            high=std::min(high,int(std::floor(center.y))+5);
            // Grow upward from the existing surface.
            for(int y=low;y<=high;y++){
                const Cell cell{x,y,z},root=parent(cell,n);
                if(root.y<1 || root.y>=heightLimit() || root.x< -2048 || root.x>=2048 || root.z< -2048 || root.z>=2048)continue;
                const auto state=partition(level,cell);
                Leaf leaf{cell,level,state.material};
                if(state.divided || !allowed(leaf))break;
                const bool empty=!sample(level,cell).amount;
                if(!empty && harvestMaterial(state.material)!=material){
                    if(y+1<=elevation+1e-6)continue;
                    break;
                }
                const auto* previous=empty?nullptr:leafRoof(leaf);
                Heights before=heights(cell,previous,y+(empty?0:1)),base=ground;
                for(int i=0;i<4;i++){before[i]-=y;base[i]-=y;}
                const auto original=soilUnits(before);
                if(original==FullBlock/64)continue;
                leaf.material=material;
                cells.push_back({leaf,before,base,offset,original,empty});
            }
        }

    const auto plan=[&](double strength,std::vector<Edit>* edits){
        std::int64_t units=0;
        for(const auto& cell:cells){
            SoilHeights packed{};
            Heights after;
            for(int i=0;i<4;i++){
                const double goal=cell.ground[i]+strength*cell.offset[i];
                const double height=cell.empty?goal:std::max(goal,cell.before[i]);
                packed[i]=std::int16_t(std::lround(std::clamp(height,-100.0,100.0)*256));
                after[i]=packed[i]/256.0;
            }
            const auto final=soilUnits(after);
            if(cell.empty && final<FullBlock/2048)continue;
            const auto change=(final-cell.original)*unitScale;
            if(change<=0)continue;
            units+=change;
            if(edits){
                Edit edit{cell.leaf.level,cell.leaf.position,final?material:Air};
                if(final && final<FullBlock/64)edit.surface=packed;
                edits->push_back(edit);
            }
        }
        return units;
    };
    double strength=1;
    if(plan(1,nullptr)>budget){
        double lo=0,hi=1;
        for(int i=0;i<16;i++){
            const double mid=(lo+hi)*0.5;
            if(plan(mid,nullptr)<=budget)lo=mid;else hi=mid;
        }
        strength=lo;
    }
    std::vector<Edit> edits;
    const auto units=plan(strength,&edits);
    if(!units || units>budget)return 0;
    for(const auto& edit:edits)apply(edit);
    return units;
}

// Build the complete transaction before touching the edit tree. The unit is
// scale-relative, like exact mining/placement; no rounding of individual cuts.
// A partial natural roof counts according to its occupied lattice samples.
std::int64_t World::wholeStroke(Vec point,int tier,int material,bool deposit,std::int64_t budget,
                                const std::function<bool(Leaf)>& allowed,Vec normal){
    if(tier<0 || tier>FineDepth || !sculptable(material) || budget<FullBlock)return 0;
    const int level=std::min(tier+1,FineDepth),n=subdivisions(level);
    const int count=level==tier?1:64;
    if(length(normal)<.5)normal={0,1,0};
    normal=unit(normal);
    const Vec impact=point*n;
    const Vec center=impact+normal*(deposit?0.75:-0.65);
    const Cell anchor=floorCell(impact+normal*(deposit?.01:-.01));
    // The stroke is local, even when a large vein is connected underground.
    const int reach=count==1?2:6;
    const auto valid=[&](Cell p){
        const auto root=parent(p,n);
        return std::abs(p.x-anchor.x)<=reach && std::abs(p.y-anchor.y)<=reach &&
               std::abs(p.z-anchor.z)<=reach && root.y>=1 && root.y<heightLimit() &&
               root.x>=-2048 && root.x<2048 && root.z>=-2048 && root.z<2048;
    };
    const auto eligible=[&](Cell p){
        if(!valid(p))return false;
        const auto state=partition(level,p);
        const auto block=sample(level,p);
        if(deposit)return !state.divided && !state.material && !block.amount && allowed({p,level,material});
        if(block.amount!=volume(level) || harvestMaterial(block.id)!=material ||
           (protectedMaterial(block.id) && block.id!=CampTile) || !allowed({p,level,block.id}))return false;
        if(state.divided){
            const auto kinds=composition(level,p);
            for(int id=1;id<int(kinds.size());++id)
                if(kinds[id] && (harvestMaterial(id)!=material || (protectedMaterial(id) && id!=CampTile)))return false;
        }
        return true;
    };
    const auto distance=[&](Cell p){const Vec d=p.v()+Vec{.5,.5,.5}-center;return dot(d,d);};
    const auto nearer=[&](Cell a,Cell b){const double da=distance(a),db=distance(b);return da==db?a>b:da>db;};
    std::priority_queue<Cell,std::vector<Cell>,decltype(nearer)> frontier(nearer);
    std::unordered_set<Cell,CellHash> seen;
    const auto enqueue=[&](Cell p){if(valid(p) && seen.insert(p).second && eligible(p))frontier.push(p);};
    // Find the nearest available cell on the struck surface. Deposits must
    // begin against solid matter; the remainder grows from that first cell.
    std::optional<Cell> start;
    for(int z=-2;z<=2;++z)for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x){
        const Cell p=anchor+Cell{x,y,z};
        if(!eligible(p))continue;
        if(deposit && !std::any_of(Sides.begin(),Sides.end(),[&](Cell d){return sample(level,p+d).solid();}))continue;
        if(!start || distance(p)<distance(*start))start=p;
    }
    if(!start)return 0;
    enqueue(*start);
    std::vector<Cell> cells;
    while(!frontier.empty() && int(cells.size())<count){
        const Cell p=frontier.top();frontier.pop();cells.push_back(p);
        for(auto d:Sides)enqueue(p+d);
    }
    if(int(cells.size())!=count)return 0;
    for(auto p:cells)apply({level,p,deposit?material:int(Air)});
    return FullBlock;
}
