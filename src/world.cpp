#include "world.hpp"
#include "settlement.hpp"
#include "natural-world.hpp"
#include <fstream>
#include <limits>
#include <bit>
#include <stdexcept>
#include <utility>
#include <unordered_set>
#include <chrono>

namespace {
constexpr std::uint64_t allChildren=~std::uint64_t{};
constexpr std::array names={"Air","Grass","Dirt","Stone","Copper ore","Iron ore","Coal","Lodestone",
    "Timber","Leaves","Clay","Camp tile","Sand","Quartz","Water","Barrier","Stone pick","Kiln",
    "Copper","Iron","Coil","Crank","Magnet","Generator","Cable","Lamp","Switch","Terminal",
    "Enamel","Steel","Copper crystal","Iron crystal","Iron pick","Quarry","North","Machine Crafter",
    "Pulse Pistol","Pulse Rounds","Quartz Sight","Wearable Battery","Door","Chair","Lantern","Plank","Roof Tile",
    "Open Door","Sloped Plank","Sloped Tile","Prism Press","Pulse Crystal","Pick Tempering","Pulse Rifle","Resource Generator","House Builder","Meadow Gate","Quartz Gate","Crank Generator","Solar Panel"};
int address(Cell c) { c=c-parent(c)*4; return c.x+4*(c.y+4*c.z); }
}

const char* materialName(int id) { return id>=0 && id<int(names.size())?names[id]:"Unknown"; }
int harvestMaterial(int id) { return id==Grass?Dirt:id==Leaves?Timber:id; }
bool protectedMaterial(int id) { return id==Air || id==Water || id==Bedrock || id==CampTile; }
bool requiresPick(int id) { return id==CopperOre || id==IronOre || id==Coal || id==Lodestone || id==Clay || id==Quartz; }

Node* Node::branch(int index) {
    for (auto& pair:branches) if (pair.first==index) return pair.second.get();
    return nullptr;
}
const Node* Node::branch(int index) const {
    for (const auto& pair:branches) if (pair.first==index) return pair.second.get();
    return nullptr;
}
int Node::representative() const {
    if (!amount) return Air;
    if (fill && branches.size()<64) return fill;
    for (const auto& pair:branches) if (pair.second->amount) return pair.second->representative();
    return Air;
}

bool World::validAssets() const {
    std::error_code error;
    for (int row=-64;row<64;++row) {
        auto file=data/("row-"+std::to_string(row)+".bin");
        if (std::filesystem::file_size(file,error)!=8388608 || error) return false;
    }
    return true;
}

const Tile& World::require(int x,int z) {
    const Cell key{x,0,z};
    if (auto found=tiles.find(key);found!=tiles.end()) return found->second.material;
    if (x < -64 || x >= 64 || z < -64 || z >= 64) {
        static const Tile empty{};
        return empty;
    }
    TerrainTile incoming;
    if(auto job=pendingTiles.find(key);job!=pendingTiles.end()){
        incoming=job->second.get();pendingTiles.erase(job);
    }else incoming=generateTile(data,x,z,seed,sculptedTerrain);
    if(std::max(std::abs(x-streamedTile.x),std::abs(z-streamedTile.z))>streamedRadius+1)
        streamedRadius=-1;
    return tiles.emplace(key,std::move(incoming)).first->second.material;
}

TerrainTile World::generateTile(const std::filesystem::path& data,int x,int z,
                                std::optional<std::uint64_t> seed,bool sculptedTerrain){
    TerrainTile incoming{};
    const auto row=data/("row-"+std::to_string(z)+".bin");
    std::ifstream input(row,std::ios::binary);
    // Keep the shipped 64-high row files byte-for-byte compatible. Expand
    // each column into the taller runtime tile, leaving new cells as air.
    std::array<std::uint8_t,32*32*ArchiveHeight> archived{};
    input.seekg(std::streamoff(x+64)*archived.size());
    if (!input.read(reinterpret_cast<char*>(archived.data()),archived.size()))
        throw std::runtime_error("Could not read terrain row "+row.string()+". Extract the full ZIP again.");
    if (std::any_of(archived.begin(),archived.end(),[](int value){return value>Bedrock;}))
        throw std::runtime_error("Terrain row "+row.string()+" contains invalid block data. Extract the full ZIP again.");
    for(int col=0;col<32*32;++col)
        std::copy_n(archived.begin()+col*ArchiveHeight,ArchiveHeight,incoming.material.begin()+col*ForestWorldHeight);
    // Modern seeded worlds no longer inherit *any* of the 1 GiB archive's
    // authored starter camp, quarry trail, pillars, bridges or resource rows.
    // Each column (including the original camp) is rebuilt from continuous,
    // seed-stable landscape. Player edits live in `modified` and remain intact.
    if(seed && sculptedTerrain){
        // Authored camp and quarry matter must not survive in the air above the roof.
        for(int col=0;col<32*32;++col)
            std::fill_n(incoming.material.begin()+col*ForestWorldHeight+1,ForestWorldHeight-1,Air);
        const auto forest=natural::treesNearTile(x,z,*seed);
        // All four columns share these height samples. Evaluate each corner once.
        std::array<float,33*33> heights{};
        for(int dz=0;dz<=32;++dz)for(int dx=0;dx<=32;++dx)
            heights[dz*33+dx]=natural::surfaceHeight(x*32+dx,z*32+dz,*seed);
        constexpr std::array<Cell,4> corners={{{0,0,0},{1,0,0},{1,0,1},{0,0,1}}};
        for(int dz=0;dz<32;++dz)for(int dx=0;dx<32;++dx){
            const int gx=x*32+dx,gz=z*32+dz,column=(dz*32+dx)*ForestWorldHeight;
            TerrainRoof roof{};
            for(int i=0;i<4;++i){
                roof.elevation[i]=heights[(dz+corners[i].z)*33+dx+corners[i].x];
            }
            incoming.roofs[dz*32+dx]=roof;
            const int capLow=std::clamp(int(std::floor(roof.low())),1,ForestWorldHeight-2);
            const int capHigh=std::clamp(int(std::ceil(roof.high()))-1,capLow,ForestWorldHeight-2);
            // No ore/stone confetti painted over the grass. Veins remain
            // entirely underground, while actual shorelines can be sandy.
            const bool sandy=roof.high()<24.5;
            const int surface=sandy?Sand:natural::trailSurface(gx+.5,gz+.5,*seed)?Dirt:Grass;
            for(int y=1;y<=std::max(capHigh,23);++y){
                int material=Air;
                if(y<=capHigh){
                    // IMPORTANT: all root cells intersecting the SAME roof carry
                    // the same surface material. Previously only the uppermost
                    // cell was grass and lower roof fragments exposed dirt in
                    // triangular rings on otherwise grassy hillsides.
                    if(y>=capLow)material=surface;
                    else if(y>=capLow-3)material=sandy?Sand:Dirt;
                    else {
                        // Geology is INSIDE the ground, in contiguous veins.
                        // There are no above-ground single cubes or free-floating
                        // material shelves in newly generated terrain.
                        const auto vein=natural::oreCluster(gx,y,gz,*seed);
                        const auto speck=natural::hash(gx+13*y,gz-7*y,*seed,0xa113u);
                        const int ore=int(vein%127);
                        material=ore<4 && (speck&3)!=0?CopperOre:
                            ore<8 && (speck&3)!=0?IronOre:
                            ore<12 && (speck&3)!=0?Coal:
                            ore==12 && (speck&3)!=0?Lodestone:
                            ore==13 && (speck&3)!=0?Quartz:Stone;
                    }
                }else if(y<=23)material=Water;
                incoming.material[column+y]=static_cast<std::uint8_t>(material);
            }
        }
        // Fill prepared volumes only where they overlap this tile. Timber
        // wins over foliage; leaf growth cannot replace the ground.
        const auto writeTree=[&](int gx,int y,int gz,int part){
            const int index=((gz-z*32)*32+gx-x*32)*ForestWorldHeight+y;
            if(part==2 || (part==1 && (incoming.material[index]==Air || incoming.material[index]==Water))){
                incoming.material[index]=part==2?Timber:Leaves;
                incoming.livingTrees.set(index);
            }
        };
        const Bounds tile{{x*32,1,z*32},{x*32+32,ForestWorldHeight-1,z*32+32}};
        for(const auto& tree:forest)if(!tree.village)natural::rasterTree(tree,tile,writeTree);
        for(int dz=0;dz<32;++dz)for(int dx=0;dx<32;++dx){
            const int gx=x*32+dx,gz=z*32+dz,column=(dz*32+dx)*ForestWorldHeight;
            const auto& roof=incoming.roofs[dz*32+dx];
            const int capLow=std::clamp(int(std::floor(roof.low())),1,ForestWorldHeight-2);
            const int capHigh=std::clamp(int(std::ceil(roof.high()))-1,capLow,ForestWorldHeight-2);
            const double localGround=roof.height(.5,.5);
            for(const auto& tree:forest){
                if(!tree.village){
                    if(std::abs(gx-tree.x)>11 || std::abs(gz-tree.z)>11)continue;
                    const bool stem=natural::treeMatter(tree,gx,tree.base,gz)==2;
                    const int top=std::min(ForestWorldHeight-2,std::max(capHigh+2,stem?tree.base:0));
                    for(int y=std::max(1,capLow-3);y<=top;++y){
                        if((stem && y<=tree.base && y>=capLow-2) || natural::rootMatter(tree,gx,y,gz,localGround))
                            writeTree(gx,y,gz,2);
                    }
                    continue;
                }
                if(std::abs(gx-tree.x)>natural::TreeReach || std::abs(gz-tree.z)>natural::TreeReach)continue;
                // Most columns touch no tree in this tile. Reject them before
                // scanning the vertical range or testing every branch.
                bool possible=std::hypot(gx-tree.x,gz-tree.z)<(tree.elder?10.0:8.0);
                for(const auto& crown:tree.boughs)
                    if(std::abs(gx-tree.x-crown.x)<crown.rx*1.6+2 &&
                       std::abs(gz-tree.z-crown.z)<crown.rz*1.6+2)possible=true;
                // Branches can be between the bole and a crown.
                if(!possible)for(const auto& crown:tree.boughs)
                    if(gx-tree.x>=std::min(0.0,crown.x)-2 && gx-tree.x<=std::max(0.0,crown.x)+2 &&
                       gz-tree.z>=std::min(0.0,crown.z)-2 && gz-tree.z<=std::max(0.0,crown.z)+2)possible=true;
                if(!possible)continue;
                const int bottom=std::max(1,std::min(capLow-3,tree.base-4));
                const bool stem=natural::treeMatter(tree,gx,tree.base,gz)==2;
                const int top=std::min(ForestWorldHeight-2,tree.base+tree.height+7);
                for(int y=bottom;y<=top;++y){
                    const int part=natural::treeMatter(tree,gx,y,gz);
                    const bool root=std::abs(y+.5-localGround)<=2.2 && natural::rootMatter(tree,gx,y,gz,localGround);
                    const bool foundation=stem && y<=tree.base && y>=capLow-2;
                    if(part==2 || root || foundation){
                        incoming.material[column+y]=Timber;
                        incoming.livingTrees.set(column+y);
                    }else if(part==1 && (incoming.material[column+y]==Air ||
                                       incoming.material[column+y]==Water)){
                        incoming.material[column+y]=Leaves;
                        incoming.livingTrees.set(column+y);
                    }
                }
            }
            // Rare, physically present canopy settlements. Their timber,
            // decking, small houses and lanterns share the world voxel grid,
            // allowing the player to approach, edit and later scale into them.
            for(const auto& tree:forest){
                if(!tree.village || std::abs(gx-tree.x)>7 || std::abs(gz-tree.z)>7)continue;
                const int bottom=std::max(capHigh+1,tree.base+tree.height-19);
                const int top=std::min(ForestWorldHeight-2,tree.base+tree.height-5);
                for(int y=bottom;y<=top;++y){
                    const int deck=tree.base+tree.height-10;
                    // A walkable pocket sits inside the living canopy. Without
                    // it the small dwellings would be hidden inside foliage.
                    if(y>=deck && y<=deck+4 &&
                       std::hypot(gx-tree.x,gz-tree.z)<6.5 &&
                       incoming.material[column+y]==Leaves){
                        incoming.material[column+y]=Air;
                        incoming.livingTrees.reset(column+y);
                    }
                    const int part=natural::villageMatter(tree,gx,y,gz);
                    if(!part || (part==2 && incoming.material[column+y]==Timber))continue;
                    incoming.material[column+y]=static_cast<std::uint8_t>(
                        part==1?Timber:part==2?Plank:part==3?RoofTile:Lantern);
                    incoming.livingTrees.reset(column+y);
                }
            }
            // No old sculpted hollows, tutorial flooring, quarry geometry,
            // generated pole-trunks, or above-roof resource deposits survive.
            // Bedrock y=0 remains exactly as in the original world data.
        }
    }else{
        if(seed){
            const auto hash=[&](int x,int z,std::uint32_t salt){
                return mix(std::uint32_t(x)*0x9e3779b9u ^ std::uint32_t(z)*0x85ebca6bu ^
                    mix(std::uint32_t(*seed) ^ salt) ^ mix(std::uint32_t(*seed>>32)));
            };
            for(int dz=0;dz<32;dz++)for(int dx=0;dx<32;dx++){
                const int gx=x*32+dx,gz=z*32+dz,column=(dz*32+dx)*ForestWorldHeight;
                const double corridor=std::max({-14-gx,gx-87,-13-gz,gz-36});
                if(corridor<=0)continue;
                const int height=int(natural::frontierHeight(gx,gz,*seed));
                TerrainRoof profile{};
                if(sculptedTerrain && corridor>23){
                    constexpr std::array<Cell,4> corners={{{0,0,0},{1,0,0},{1,0,1},{0,0,1}}};
                    for(std::size_t i=0;i<corners.size();i++){
                        const int xx=gx+corners[i].x,zz=gz+corners[i].z;
                        profile.elevation[i]=float(natural::frontierHeight(xx,zz,*seed));
                    }
                    incoming.roofs[dz*32+dx]=profile;
                }
                int original=63;
                while(original>0 && incoming.material[column+original]==Air)original--;
                const int top=profile.elevation[0]>0?int(std::ceil(profile.high()))-1:
                    std::clamp(int(std::lerp(double(original),double(height),std::min(corridor/22,1.0))),8,56);
                const bool sandy=top<=24;
                for(int y=1;y<64;y++){
                    int material=Air;
                    if(y<=top){
                        if(y==top)material=sandy?Sand:Grass;
                        else if(y>=top-3)material=sandy?Sand:Dirt;
                        else {
                            const auto ore=hash(divFloor(gx,3),divFloor(gz,3),std::uint32_t(y/3)+89)%170;
                            material=ore<3?IronOre:ore<6?CopperOre:ore<10?Coal:ore==10?Lodestone:ore==11?Quartz:Stone;
                        }
                    }else if(y<=23)material=Water;
                    incoming.material[column+y]=material;
                }
                if(!sandy && top<51 && hash(gx,gz,50)%63==0 && profile.elevation[0]==0)
                    for(int y=1;y<=5;y++)incoming.material[column+top+y]=y<4?Timber:Leaves;
            }
        }
        if(sculptedTerrain)for(int dz=0;dz<32;dz++)for(int dx=0;dx<32;dx++){
            const int gx=x*32+dx,gz=z*32+dz;
            auto& roof=incoming.roofs[dz*32+dx];
            if(roof.elevation[0]>0)continue;
            const bool authored=(std::abs(gx)<19 && gz>=-13 && gz<=16) ||
                (gx>52 && gx<88 && gz>12 && gz<37);
            const int column=(dz*32+dx)*ForestWorldHeight;
            int y=62;
            while(y>8 && incoming.material[column+y]==Air)y--;
            const int id=incoming.material[column+y];
            if(authored && id!=Grass)continue;
            if(id!=Grass && id!=Dirt && id!=Sand && id!=Clay && id!=Stone)continue;
            constexpr std::array<Cell,4> corner={{{0,0,0},{1,0,0},{1,0,1},{0,0,1}}};
            for(int i=0;i<4;i++){
                const auto hash=CellHash{}({gx+corner[i].x,0,gz+corner[i].z});
                roof.elevation[i]=float(y+0.60+(hash&31)*0.012);
            }
        }
        // Overwrite the *authored* opening camp, including rows baked into the 1 GiB
        // world archive. This is done at runtime so existing world data need not be
        // regenerated. Saves' player edits remain in the separate modification tree.
        if(sculptedTerrain)for(int dz=0;dz<32;++dz)for(int dx=0;dx<32;++dx){
            const int gx=x*32+dx,gz=z*32+dz;
            if(!natural::starterArea(gx,gz))continue;
            const int column=(dz*32+dx)*ForestWorldHeight;
            // Use the same rim height at every corner. Sampling separate old voxel
            // columns here would re-introduce cracks at the boundary of old walls.
            constexpr double rimHeight=29.0;
            auto& roof=incoming.roofs[dz*32+dx];
            constexpr std::array<Cell,4> corners={{{0,0,0},{1,0,0},{1,0,1},{0,0,1}}};
            for(int corner=0;corner<4;++corner){
                const double xx=gx+corners[corner].x,zz=gz+corners[corner].z;
                const double strength=natural::starterFade(xx,zz);
                roof.elevation[corner]=std::lerp(rimHeight,
                    natural::starterHeight(xx,zz,seed.value_or(62108432)),strength);
            }
            const int top=std::clamp(int(std::ceil(roof.high()))-1,2,61);
            for(int y=1;y<64;++y){
                int material=Air;
                if(y<=top){
                    if(y==top)material=Grass;
                    else if(y>=top-3)material=Dirt;
                    else {
                        const auto mineral=natural::hash(gx/4+3*y,gz/4-y,seed.value_or(0),0x9d3u)%160;
                        material=mineral<4?CopperOre:mineral<7?IronOre:mineral<10?Coal:Stone;
                    }
                }else if(y<=23)material=Water;
                incoming.material[column+y]=static_cast<std::uint8_t>(material);
                incoming.surface.erase(column+y);
            }
            // Buildable resources are scattered as discrete deposits, not the
            // original pre-placed cubic rows. The mineral mesh shapes them.
            const double radius=std::hypot(gx-0.5,gz+7.0);
            const auto token=natural::hash(gx,gz,seed.value_or(0),0x45f1u);
            if(radius>8.0 && radius<18.0 && token%67==0 && top<62){
                const int resource=(token>>10)%5;
                incoming.material[column+top+1]=static_cast<std::uint8_t>(
                    resource==0?CopperOre:resource==1?IronOre:resource==2?Coal:
                    resource==3?Clay:Stone);
            }
        }
        // Natural exposed banks contain small hollows. The shape is fixed by world position
        // so a moving detail boundary never changes which material is actually there.
        for (int dz=0;dz<32;++dz) for(int dx=0;dx<32;++dx) {
            const int gx=x*32+dx,gz=z*32+dz;
            if (natural::starterArea(gx,gz) ||
                (gx>=2 && gx<=83 && gz>=14 && gz<=34)) continue;
            const int column=(dz*32+dx)*ForestWorldHeight;
            if(incoming.roofs[dz*32+dx].elevation[0]>0)continue;
            for (int y=22;y<63;y++) {
                const int id=incoming.material[column+y];
                if (id!=Grass && id!=Dirt && id!=Stone && id!=Sand && id!=Clay) continue;
                if (incoming.material[column+y+1]!=Air) continue;
                const auto seed=CellHash{}({gx,y,gz});
                std::uint64_t mask=allChildren;
                for (int az=0;az<4;az++)for(int ax=0;ax<4;ax++) {
                    const int child=ax+4*(3+4*az);
                    if (mix(seed+std::uint32_t(child)*27971u)%100 < (id==Grass?22u:36u))
                        mask &= ~(std::uint64_t{1}<<child);
                }
                if (mask!=allChildren) incoming.surface.emplace(column+y,mask);
            }
        }
    }
    return incoming;
}

void World::stream(Cell center,int radius) {
    auto tile=parent(center,32);
    tile.y=0;
    if(tile==streamedTile && radius<=streamedRadius)return;
    for (int z=tile.z-radius;z<=tile.z+radius;++z)
        for (int x=tile.x-radius;x<=tile.x+radius;++x) require(x,z);
    std::erase_if(tiles,[&](const auto& pair){
        return std::abs(pair.first.x-tile.x)>radius+1 || std::abs(pair.first.z-tile.z)>radius+1;
    });
    streamedTile=tile;
    streamedRadius=radius;
}

void World::streamInteractive(Cell center,int radius){
    if(!seed || !sculptedTerrain || generated){stream(center,radius);return;}
    auto tile=parent(center,32);tile.y=0;
    // Publish completed tiles on the main thread only. Never expose half-built
    // matter to collision, ray casts, saves, or the OpenGL renderer.
    for(auto it=pendingTiles.begin();it!=pendingTiles.end();){
        if(it->second.wait_for(std::chrono::seconds(0))!=std::future_status::ready){++it;continue;}
        auto ready=it->second.get();const auto key=it->first;it=pendingTiles.erase(it);
        if(std::abs(key.x-tile.x)<=radius+1 && std::abs(key.z-tile.z)<=radius+1)
            tiles.try_emplace(key,std::move(ready));
    }
    // Physics always has a complete 3x3 safety neighborhood. Normal travel
    // reaches it after the background prefetch; teleporting can still block.
    for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx)require(tile.x+dx,tile.z+dz);
    std::erase_if(tiles,[&](const auto& pair){
        return std::abs(pair.first.x-tile.x)>radius+1 || std::abs(pair.first.z-tile.z)>radius+1;
    });
    // Two CPU workers maximum on the laptop. Nearest missing tiles first.
    for(int ring=2;ring<=radius+1 && pendingTiles.size()<2;++ring)
        for(int dz=-ring;dz<=ring && pendingTiles.size()<2;++dz)
            for(int dx=-ring;dx<=ring && pendingTiles.size()<2;++dx){
                if(std::max(std::abs(dx),std::abs(dz))!=ring)continue;
                const Cell key{tile.x+dx,0,tile.z+dz};
                if(tileReady(key.x,key.z) || pendingTiles.contains(key))continue;
                pendingTiles.emplace(key,std::async(std::launch::async,[path=data,key,seed=seed,sculpt=sculptedTerrain]{
                    return generateTile(path,key.x,key.z,seed,sculpt);
                }));
            }
    streamedTile=tile;streamedRadius=1;
}

void World::defineRoof(Cell column,TerrainRoof profile){
    sculptedTerrain=true;
    column.y=0;
    localRoofs.insert_or_assign(column,profile);
}

const TerrainRoof* World::rawRoof(Cell column) const {
    if(!sculptedTerrain)return nullptr;
    if(generated){
        const auto found=localRoofs.find({column.x,0,column.z});
        return found==localRoofs.end()?nullptr:&found->second;
    }
    const Cell tile{divFloor(column.x,32),0,divFloor(column.z,32)};
    const auto found=tiles.find(tile);
    if(found==tiles.end())return nullptr;
    const int x=column.x-tile.x*32,z=column.z-tile.z*32;
    const auto& profile=found->second.roofs[z*32+x];
    return profile.elevation[0]>0?&profile:nullptr;
}

const TerrainRoof* World::roof(Cell column) const {
    return rawRoof(column);
}

bool World::naturalTree(Cell root) const {
    if(root.y<0 || root.y>=heightLimit())return false;
    const auto tile=parent(root,32);
    const auto found=tiles.find({tile.x,0,tile.z});
    if(found==tiles.end())return false;
    const int index=((root.z-tile.z*32)*32+root.x-tile.x*32)*ForestWorldHeight+root.y;
    return found->second.livingTrees.test(index);
}

int World::uneditedTreeMaterial(Cell root) const {
    if(root.y<0 || root.y>=heightLimit())return Air;
    const auto tile=parent(root,32);
    const auto found=tiles.find({tile.x,0,tile.z});
    if(found==tiles.end())return Air;
    const int index=((root.z-tile.z*32)*32+root.x-tile.x*32)*ForestWorldHeight+root.y;
    const auto& t=found->second;
    if(!t.livingTrees.test(index) || modified.contains(root))return Air;
    const int id=t.material[index];
    if(id!=Timber && id!=Leaves)return Air;
    // Modern generated tree voxels have no fractional surface masks. Retain
    // the exact fallback for other terrain types and future generators.
    if(t.surface.contains(index) || disturbed.contains(root)){
        const auto b=sample(0,root);return b.amount==FullVolume?b.id:int(Air);
    }
    return id;
}

bool World::treeThroughRoof(Cell root) const {
    // Generated micro-world trunks are deliberately rooted into the terrain
    // profile, just like streamed surface trees. Never clip wood at the soil
    // surface if a continuous trunk passes through that root cell.
    if(generated)return generated(root)==Timber && generated(root+Cell{0,1,0})==Timber;
    if(root.y<0 || root.y>=heightLimit()-1)return false;
    const auto tile=parent(root,32);
    const auto found=tiles.find({tile.x,0,tile.z});
    if(found==tiles.end())return false;
    const int index=((root.z-tile.z*32)*32+root.x-tile.x*32)*ForestWorldHeight+root.y;
    const auto& t=found->second;
    return t.livingTrees.test(index) && t.livingTrees.test(index+1) &&
           t.material[index]==Timber && t.material[index+1]==Timber;
}

const TerrainRoof* World::leafRoof(Leaf leaf) const {
    const Cell root=parent(leaf.position,subdivisions(leaf.level));
    const auto* profile=roof(root);
    if(leaf.material==Air || leaf.material==Water)return nullptr;
    // The terrain roof clips soil, not a vertical stem crossing it.
    // Otherwise collision and fine-scale sampling leave an air slice
    // between the roof and the next timber voxel.
    if(profile && (!profile->covers(root.y) || treeThroughRoof(root)))profile=nullptr;
    const auto changed=modified.find(root);
    if(changed==modified.end())return profile;
    const Node* node=changed->second.get();
    for(int depth=1;depth<=leaf.level;depth++){
        const auto* child=node->branch(address(parent(leaf.position,subdivisions(leaf.level-depth))));
        if(!child)break;
        node=child;
    }
    if(node->surface)return node->surface.get();
    return node->built?nullptr:profile;
}

std::pair<int,std::uint64_t> World::base(Cell cell) const {
    const TerrainTile* source=nullptr;
    int index=0,material=Air;
    if(generated)material=generated(cell);
    else {
        if(cell.y<0)return {Bedrock,allChildren};
        if(cell.y>=heightLimit())return {Air,0};
        const auto tile=parent(cell,32);
        const auto found=tiles.find({tile.x,0,tile.z});
        if(found==tiles.end())return {Air,0};
        source=&found->second;
        index=((cell.z-tile.z*32)*32+cell.x-tile.x*32)*ForestWorldHeight+cell.y;
        material=source->material[index];
    }
    if(!material)return {Air,0};
    if(sculptedTerrain && material!=Water && disturbed.contains(cell)){
        const auto* profile=rawRoof(cell);
        if(profile && profile->covers(cell.y)){
            std::uint64_t mask=0;
            for(int z=0;z<4;z++)for(int x=0;x<4;x++){
                for(int y=0;y<4;y++){
                    const Vec start{x*0.25,cell.y+y*0.25,z*0.25};
                    Box part{start,start+Vec{0.25,0.25,0.25}};
                    if(profile->occupied(part,start.y))mask|=std::uint64_t{1}<<(x+4*(y+4*z));
                }
            }
            return mask?std::pair{material,mask}:std::pair{0,std::uint64_t{0}};
        }
    }
    if(source){
        const auto shape=source->surface.find(index);
        if(shape!=source->surface.end())return {material,shape->second};
    }
    return {material,allChildren};
}

const Node* World::edited(Cell pos,int level,int& fallback) const {
    const Cell root=parent(pos,subdivisions(level));
    const auto found=modified.find(root);
    if (found==modified.end()) return nullptr;
    const Node* node=found->second.get();
    for (int depth=1;depth<=level;++depth) {
        fallback=node->fill;
        node=node->branch(address(parent(pos,subdivisions(level-depth))));
        if (!node) return nullptr;
    }
    return node;
}

Sample World::sample(int level,Cell pos) const {
    int inherited=-1;
    const auto* node=edited(pos,level,inherited);
    Sample result;
    if(node)result={node->representative(),node->amount};
    else if(inherited>=0)result={inherited,inherited?volume(level):0};
    else{
        const auto [id,shape]=base(parent(pos,subdivisions(level)));
        if(!level)return {id,std::popcount(shape)*volume(1)};
        const int child=address(parent(pos,subdivisions(level-1)));
        if(shape&(std::uint64_t{1}<<child))result={id,volume(level)};
    }
    if(level>0 && result.amount){
        const Leaf leaf{pos,level,result.id};
        if(const auto* profile=leafRoof(leaf)){
            const auto root=parent(pos,subdivisions(level));
            const Vec anchor{double(root.x),0,double(root.z)};
            const auto bounds=leaf.bounds();
            Box local{bounds.lo-anchor,bounds.hi-anchor};
            if(!profile->occupied(local,local.lo.y))return {};
        }
    }
    return result;
}

void World::count(const Node& node,int level,Composition& output) const {
    if (!node.amount) return;
    if (node.branches.empty()) {
        output[node.fill]+=node.amount;
        return;
    }
    const int depth=level+1;
    const auto portion=volume(depth);
    if (node.fill)output[node.fill]+=volume(level)-MatterVolume(node.branches.size())*portion;
    for (const auto& branch:node.branches) count(*branch.second,depth,output);
}

Composition World::composition(int level,Cell pos) const {
    Composition result{};
    int inherited=-1;
    if (const Node* node=edited(pos,level,inherited)) count(*node,level,result);
    else if (inherited>=0) {
        if(inherited)result[inherited]=volume(level);
    } else {
        const auto part=sample(level,pos);
        result[part.id]=part.amount;
    }
    return result;
}

MatterVolume World::write(Node& current,int layer,const Edit& edit,bool profile) {
    const auto previous=current.amount;
    if (layer==edit.level) {
        current.fill=edit.id;
        current.built=(profile || edit.surface.has_value()) && edit.id!=Air;
        current.surface.reset();
        if(edit.surface){
            TerrainRoof patch;
            const int n=subdivisions(layer);
            const Cell local=edit.position-parent(edit.position,n)*n;
            patch.span=1.0/n;patch.x0=local.x*patch.span;patch.z0=local.z*patch.span;
            for(int i=0;i<4;i++)patch.elevation[i]=(edit.position.y+(*edit.surface)[i]/256.0)/n;
            current.surface=std::make_shared<TerrainRoof>(patch);
        }
        current.amount=edit.id?volume(layer):0;
        current.branches.clear();
        return current.amount-previous;
    }
    int index=address(parent(edit.position,subdivisions(edit.level-layer-1)));
    auto child=current.branch(index);
    if (!child) {
        current.branches.emplace_back(index,std::make_unique<Node>(current.fill,volume(layer+1)));
        child=current.branches.back().second.get();
        child->built=current.built;
        child->surface=current.surface;
    }
    current.amount+=write(*child,layer+1,edit,profile);
    if (child->fill==current.fill && child->branches.empty() && (!child->fill || (child->built==current.built && child->surface==current.surface)))
        std::erase_if(current.branches,[&](const auto& entry){return entry.first==index;});
    if (current.amount==0) {
        current.branches.clear();
        current.fill=Air;
    }
    return current.amount-previous;
}

void World::clearEdits() { disturbed.clear(); modified.clear(); changes.clear(); changedTiles.clear(); changedTreeTiles.clear(); changedRoots.clear(); revision_++; }

std::map<Cell,std::vector<Edit>> World::saveChunks() const {
    std::map<Cell,std::vector<Edit>> result;
    std::array<std::unordered_set<Cell,CellHash>,FineDepth+1> covered;
    for(auto it=changes.rbegin();it!=changes.rend();++it){
        const auto& edit=*it;
        bool overwritten=false;
        Cell key=edit.position;
        for(int level=edit.level;level>=0;level--,key=parent(key)){
            if(covered[level].contains(key)){overwritten=true;break;}
        }
        if(overwritten)continue;
        covered[edit.level].insert(edit.position);
        const Cell chunk=parent(parent(edit.position,subdivisions(edit.level)),32);
        result[{chunk.x,0,chunk.z}].push_back(edit);
    }
    for(auto& [chunk,entries]:result)std::reverse(entries.begin(),entries.end());
    return result;
}

std::uint64_t World::meshVersion(Cell tile) const {
    auto found=changedTiles.find({tile.x,0,tile.z});
    return found==changedTiles.end()?0:found->second;
}

std::uint64_t World::treeVersion(Cell tile) const {
    const auto found=changedTreeTiles.find({tile.x,0,tile.z});
    return found==changedTreeTiles.end()?0:found->second;
}

std::uint64_t World::detailVersion(Bounds area,int level) const {
    // Fine meshes sample one extra cell on each face to decide which faces to draw.
    const int n=subdivisions(level);
    const auto rootLo=parent(area.lo-Cell{1,1,1},n)-Cell{1,1,1};
    const auto rootHi=parent(area.hi,n)+Cell{1,1,1};
    const auto dim=rootHi-rootLo+Cell{1,1,1};
    // The fine cache depends on its root cells and a one-root bank/face halo,
    // not every edit in the surrounding 32m tile. Large queries (e.g. trail
    // placement) keep the original tile-level invalidation.
    if(std::int64_t(dim.x)*dim.y*dim.z<=4096){
        std::uint64_t version=0;
        for(int z=rootLo.z;z<=rootHi.z;++z)for(int y=rootLo.y;y<=rootHi.y;++y)
            for(int x=rootLo.x;x<=rootHi.x;++x){
                auto found=changedRoots.find({x,y,z});
                if(found!=changedRoots.end())version=std::max(version,found->second);
            }
        return version;
    }
    const int tileSize=32*n;
    const auto lo=parent(area.lo-Cell{1,1,1},tileSize);
    const auto hi=parent(area.hi,tileSize);
    std::uint64_t version=0;
    for(int z=lo.z;z<=hi.z;z++)for(int x=lo.x;x<=hi.x;x++)
        version=std::max(version,meshVersion({x,0,z}));
    return version;
}

MatterVolume World::apply(Edit edit,bool record) {
    if (edit.level<0 || edit.level>FineDepth || edit.id<0 || edit.id>Bedrock) return 0;
    if(edit.surface && !sculptable(edit.id))return 0;
    const Cell root=parent(edit.position,subdivisions(edit.level));
    if (root.x < -2048 || root.x>=2048 || root.z < -2048 || root.z>=2048 || root.y<1 || root.y>=heightLimit()) return 0;
    auto tile=parent(root,32);
    if(!generated)require(tile.x,tile.z);
    const auto before=sample(edit.level,edit.position);
    if(!edit.surface && edit.id==before.id && before.amount==(edit.id?volume(edit.level):0)){
        const auto existing=partition(edit.level,edit.position);
        if(!existing.divided && existing.material==edit.id && !leafRoof({edit.position,edit.level,edit.id}))return 0;
    }
    const auto* roof=rawRoof(root);
    const bool profiled=roof && roof->covers(root.y) && !treeThroughRoof(root);
    if(profiled)disturbed.insert(root);
    auto& tree=modified[root];
    if (!tree) {
        const auto [id,shape]=base(root);
        tree=std::make_unique<Node>(id,volume(0));
        if (id && shape!=allChildren){
            tree->amount=std::popcount(shape)*volume(1);
            for(int child=0;child<64;child++)if(!(shape&(std::uint64_t{1}<<child))){
                tree->branches.emplace_back(child,std::make_unique<Node>(Air,volume(1)));
            }
        }
    }
    const auto delta=write(*tree,0,edit,profiled);
    if (record) changes.push_back(edit);
    revision_++;
    changedRoots[root]=revision_;
    changedTiles[{tile.x,0,tile.z}]=revision_;
    // Face meshes share edges. Distant height samples also average the four
    // columns straddling a tile corner, so their diagonal owner must rebuild.
    const int lx=root.x-tile.x*32,lz=root.z-tile.z*32;
    if(lx==0 || lx==31)changedTiles[{tile.x+(lx==0?-1:1),0,tile.z}]=revision_;
    if(lz==0 || lz==31)changedTiles[{tile.x,0,tile.z+(lz==0?-1:1)}]=revision_;
    if((lx==0 || lx==31) && (lz==0 || lz==31))
        changedTiles[{tile.x+(lx==0?-1:1),0,tile.z+(lz==0?-1:1)}]=revision_;
    // Tree dual meshes sample one cell beyond each tile. Ordinary soil edits
    // leave these immutable; a natural wood/leaf edit invalidates its owners.
    if(naturalTree(root)){
        for(int dz=(lz==0?-1:0);dz<=(lz==31?1:0);++dz)
            for(int dx=(lx==0?-1:0);dx<=(lx==31?1:0);++dx)
                changedTreeTiles[{tile.x+dx,0,tile.z+dz}]=revision_;
    }
    return delta;
}

Partition World::partition(int level,Cell pos) const {
    int fill=-1;
    if(const auto* node=edited(pos,level,fill)){
        if(!node->amount)return {};
        return {node->representative(),!node->branches.empty()};
    }
    if(fill>=0)return {fill,false};
    const auto [material,shape]=base(parent(pos,subdivisions(level)));
    if(!material)return {};
    if(level==0)return {material,shape!=allChildren};
    const int child=address(parent(pos,subdivisions(level-1)));
    return shape&(std::uint64_t{1}<<child)?Partition{material,false}:Partition{};
}

BlockPattern World::pattern(Cell root) const{
    const auto found=modified.find(root);
    if(found==modified.end()){
        const auto [id,mask]=base(root);
        return {id,mask,false};
    }
    const auto& node=*found->second;
    const int id=node.representative();
    return {id,id?allChildren:0,!node.branches.empty()};
}

bool World::editedChildren(Cell root) const {
    auto found=modified.find(root);
    return found!=modified.end() && !found->second->branches.empty();
}

bool World::visit(Cell pos,int level,Box query,const std::function<bool(Leaf)>& visitor) const {
    Leaf cube{pos,level,Air};
    if(!cube.bounds().intersects(query))return true;
    const auto state=partition(level,pos);
    if(!state.material)return true;
    if(!state.divided || level>=FineDepth){
        cube.material=state.material;
        return visitor(cube);
    }
    const Cell start=pos*4;
    const double size=subdivisions(level+1);
    const auto lo=floorCell(query.lo*size),hi=floorCell(query.hi*size);
    for(int y=std::max(start.y,lo.y);y<=std::min(start.y+3,hi.y);y++)
        for(int z=std::max(start.z,lo.z);z<=std::min(start.z+3,hi.z);z++)
            for(int x=std::max(start.x,lo.x);x<=std::min(start.x+3,hi.x);x++)
                if(!visit({x,y,z},level+1,query,visitor))return false;
    return true;
}

bool World::visitMatter(Box query,const std::function<bool(Leaf)>& visitor) const {
    const auto lo=floorCell(query.lo),hi=floorCell(query.hi);
    for(int y=lo.y;y<=hi.y;++y)for(int z=lo.z;z<=hi.z;++z)for(int x=lo.x;x<=hi.x;++x)
        if(!visit({x,y,z},0,query,visitor))return false;
    return true;
}

void World::visibleFace(Leaf voxel,int side,const std::function<void(Leaf)>& visible) const {
    visitFaceNeighbors(voxel,side,[&](Leaf face,Leaf neighbor){
        if(neighbor.material==Air || (neighbor.material==Water && voxel.material!=Water))
            visible(face);
    });
}
void World::visitFaceNeighbors(Leaf voxel,int side,const std::function<void(Leaf,Leaf)>& visitor) const {
    const Cell next=voxel.position+Sides[side];
    const auto neighbor=partition(voxel.level,next);
    if(!neighbor.divided || voxel.level>=FineDepth){
        visitor(voxel,{next,voxel.level,neighbor.material});
        return;
    }
    // Subdivide only the face that touches the heterogeneous neighbor.
    const int axis=side/2,us=(axis+1)%3,vs=(axis+2)%3;
    auto offset=voxel.position*4;
    offset[axis]+=side%2?3:0;
    for(int u=0;u<4;u++)for(int v=0;v<4;v++){
        auto cell=offset;cell[us]+=u;cell[vs]+=v;
        visitFaceNeighbors({cell,voxel.level+1,voxel.material},side,visitor);
    }
}


std::optional<TerrainStone> World::shapedPiece(Leaf leaf) const {
    if(!sculptedTerrain || !sculptable(leaf.material))return {};
    const int n=subdivisions(leaf.level);
    const Cell root=parent(leaf.position,n);
    if(root.y>=latticeFrom || leafRoof(leaf))return {};

    // Cache occupancy for the small stencil. Only split or sloping neighbors
    // need a precise point query (important when a scoop just grazes ore).
    struct Neighbor {int fill=-1,material=Air;};
    std::array<Neighbor,27> stencil{};
    const auto fill=[&](Cell d){
        auto& neighbor=stencil[d.x+1+3*(d.y+1+3*(d.z+1))];
        int& f=neighbor.fill;
        if(f<0){
            const Cell p=leaf.position+d;
            const auto state=partition(leaf.level,p);
            neighbor.material=state.material;
            f=state.divided?2:(!state.material || state.material==Water)?0:
                leafRoof({p,leaf.level,state.material})?2:1;
        }
        return f;
    };
    unsigned visible=0,empty=0;
    for(int side=0;side<6;side++){
        const int f=fill(Sides[side]);
        if(f!=1)visible|=1u<<side;
        if(!f)empty|=1u<<side;
    }
    if(!visible)return {};
    const int id=leaf.material;
    if((id==Stone || id==CopperOre || id==IronOre || id==Coal || id==Lodestone || id==Quartz || id==Clay)
        && (empty==63 || (empty==59 && visible==59)))
        return TerrainStone(CellHash{}(leaf.position),id==Quartz || id==Lodestone,empty==59);

    auto bank=std::make_shared<TerrainStone::Bank>();
    bank->visible=visible;
    for(int z=0;z<=2;z++)for(int y=0;y<=2;y++)for(int x=0;x<=2;x++){
        const Cell node{x,y,z};
        Vec& point=bank->at(node);
        point=node.v()*0.5;
        if(node==Cell{1,1,1})continue;
        unsigned support=0;
        bool pinned=false,ore=false;
        for(int octant=0;octant<8;octant++){
            Cell offset{};
            Vec local=point;
            for(int axis=0;axis<3;axis++){
                const int sign=(octant&(1<<axis))?1:-1;
                local[axis]+=sign*0.000001;
                offset[axis]=int(std::floor(local[axis]));
            }
            const int f=fill(offset);
            if(!f)continue;
            int material=stencil[offset.x+1+3*(offset.y+1+3*(offset.z+1))].material;
            if(f==2){
                const Vec site=(leaf.position.v()+local)/n;
                const Cell fine=floorCell(site*subdivisions(FineDepth));
                const auto state=partition(FineDepth,fine);
                if(!state.material || state.material==Water)continue;
                const auto* cap=leafRoof({fine,FineDepth,state.material});
                const auto owner=floorCell(site);
                if(cap && site.y>cap->height(site.x-owner.x,site.z-owner.z))continue;
                material=state.material;
                pinned=true; // meet the existing clipped shovel triangle exactly
            }
            ore|=(material>=CopperOre && material<=Lodestone) || material==Quartz;
            for(int axis=0;axis<3;axis++)support|=1u<<(axis*2+((octant>>axis)&1));
        }
        if(pinned)continue;
        Vec inward{};
        int openAxes=0;
        for(int axis=0;axis<3;axis++){
            const auto sides=(support>>(axis*2))&3u;
            if(sides==1 || sides==2){inward[axis]=sides==1?-1:1;openAxes++;}
        }
        const double noise=(CellHash{}(leaf.position*2+node)&255)/255.;
        if(openAxes>=2)point+=inward*(0.21+0.16*noise);
        else if(openAxes==1 && ore)point+=inward*(0.055+0.15*noise);
        else if(openAxes==1 && inward.y==0)point+=inward*(0.025+0.065*noise);
    }
    return TerrainStone(std::move(bank));
}

std::optional<Hit> World::ray(Vec origin,Vec direction,int level,double reach) const {
    const auto ray=unit(direction);
    if(length(ray)<0.5 || reach<=0)return {};
    const int n=subdivisions(level);
    const Vec position=origin*n;
    auto cell=floorCell(position);
    Cell normal{};
    std::array<double,3> steps{}, crossings{};
    std::array<int,3> signs{};
    for (int axis=0;axis<3;++axis) {
        if (std::abs(ray[axis])<1e-10) { steps[axis]=crossings[axis]=1e18;continue; }
        signs[axis]=ray[axis]>0?1:-1;
        steps[axis]=1.0/std::abs(ray[axis]);
        crossings[axis]=(cell[axis]+(signs[axis]>0)-position[axis])/ray[axis];
    }
    Cell baseCell{std::numeric_limits<int>::min(),0,0};
    std::optional<TerrainStone> rootPiece;
    double entered=0;
    while (entered<=reach*n) {
        int axis=crossings[1]<crossings[0]?1:0;
        if (crossings[2]<crossings[axis]) axis=2;
        const double leaving=std::min(crossings[axis],reach*n);
        if (leaving>entered+1e-8) {
            const auto state=partition(level,cell);
            if(state.material!=Air && (state.divided || state.material!=Water)){
                const auto root=parent(cell,n);
                if(sculptedTerrain && root!=baseCell){
                    baseCell=root;
                    const auto base=partition(0,root);
                    rootPiece=base.divided?std::nullopt:shapedPiece({root,0,base.material});
                }
                if(rootPiece){
                    if(const auto face=rootPiece->ray(origin-root.v(),ray,entered/n,leaving/n))
                        return Hit{cell,cell+face->normal,face->normal,level,state.material,face->distance,face->surfaceNormal};
                    entered=crossings[axis];
                    crossings[axis]+=steps[axis];
                    normal={};normal[axis]=-signs[axis];
                    cell[axis]+=signs[axis];
                    continue;
                }
                const auto* profile=leafRoof({cell,level,state.material});
                if(!state.divided && profile){
                    const Vec anchor{double(root.x),0,double(root.z)};
                    const double hit=profile->intercept(origin-anchor,ray,entered/n,leaving/n);
                    if(hit<leaving/n){
                        const Cell contact=hit>entered/n+1e-10?Cell{0,1,0}:normal;
                        const Vec onRoof=origin+ray*hit-anchor;
                        const Vec face=std::abs(onRoof.y-profile->height(onRoof.x,onRoof.z))<1e-8?
                            profile->normal(onRoof.x,onRoof.z):contact.v();
                        return Hit{cell,cell+contact,contact,level,state.material,hit,face};
                    }
                    entered=crossings[axis];
                    crossings[axis]+=steps[axis];
                    normal={};normal[axis]=-signs[axis];
                    cell[axis]+=signs[axis];
                    continue;
                }
                if(!state.divided){
                    if(!sculptedTerrain || !altered(root) || level==0)
                        return Hit{cell,cell+normal,normal,level,state.material,entered/n};
                    Leaf leaf{root,0,state.material};
                    for(int depth=1;depth<=level && partition(leaf.level,leaf.position).divided;depth++)
                        leaf={parent(cell,subdivisions(level-depth)),depth,state.material};
                    if(const auto piece=shapedPiece(leaf)){
                        const auto bounds=leaf.bounds();
                        const auto size=bounds.hi.x-bounds.lo.x;
                        if(const auto hit=piece->ray((origin-bounds.lo)/size,ray,entered/n/size,leaving/n/size))
                            return Hit{cell,cell+hit->normal,hit->normal,level,state.material,hit->distance*size,hit->surfaceNormal};
                        entered=crossings[axis];
                        crossings[axis]+=steps[axis];
                        normal={};normal[axis]=-signs[axis];
                        cell[axis]+=signs[axis];
                        continue;
                    }
                    return Hit{cell,cell+normal,normal,level,state.material,entered/n};
                }
                const Vec from=origin+ray*(entered/n),to=origin+ray*(leaving/n);
                constexpr double pad=1e-8;
                Box window;
                for(int i=0;i<3;i++){
                    window.lo[i]=std::min(from[i],to[i])-pad;
                    window.hi[i]=std::max(from[i],to[i])+pad;
                }
                std::optional<Hit> first;
                visit(cell,level,window,[&](Leaf leaf){
                    if(leaf.material==Water)return true;
                    const auto limit=first?first->distance:leaving/n;
                    const auto bounds=leaf.bounds();
                    if(const auto* roof=leafRoof(leaf)){
                        const auto block=intersectRay(origin,ray,bounds,limit);
                        if(!block)return true;
                        const auto lo=std::max(block->at,entered/n);
                        const auto parentCell=parent(leaf.position,subdivisions(leaf.level));
                        const Vec anchor{double(parentCell.x),0,double(parentCell.z)};
                        double hi=limit;
                        for(int i=0;i<3;i++)if(std::abs(ray[i])>1e-10)
                            hi=std::min(hi,((ray[i]>0?bounds.hi[i]:bounds.lo[i])-origin[i])/ray[i]);
                        if(hi<=lo)return true;
                        const double hit=roof->intercept(origin-anchor,ray,lo,hi);
                        if(hit<hi){
                            const Cell contact=hit>lo+1e-10?Cell{0,1,0}:block->normal;
                            const Vec onRoof=origin+ray*hit-anchor;
                            const Vec face=std::abs(onRoof.y-roof->height(onRoof.x,onRoof.z))<1e-8?
                                roof->normal(onRoof.x,onRoof.z):contact.v();
                            first=Hit{leaf.position,leaf.position+contact,contact,leaf.level,leaf.material,hit,face};
                        }
                    }else if(const auto piece=shapedPiece(leaf)){
                        const auto size=bounds.hi.x-bounds.lo.x;
                        if(const auto hit=piece->ray((origin-bounds.lo)/size,ray,entered/n/size,limit/size))
                            first=Hit{leaf.position,leaf.position+hit->normal,hit->normal,leaf.level,leaf.material,hit->distance*size,hit->surfaceNormal};
                    }else if(const auto hit=intersectRay(origin,ray,bounds,limit))
                        first=Hit{leaf.position,leaf.position+hit->normal,hit->normal,leaf.level,leaf.material,hit->at};
                    return true;
                });
                if(first)return first;
            }
        }
        entered=crossings[axis];
        crossings[axis]+=steps[axis];
        normal={};normal[axis]=-signs[axis];
        cell[axis]+=signs[axis];
    }
    return {};
}

Box World::playerBox(Vec feet,double size,double height) {
    const double radius=size*0.24;
    return {feet+Vec{-radius,0,-radius},feet+Vec{radius,size*height,radius}};
}

std::optional<Box> World::solidBounds(Leaf leaf,Box region,double floor) const {
    if(leaf.material==Water)return {};
    auto block=leaf.bounds();
    const auto root=parent(leaf.position,subdivisions(leaf.level));
    const auto* profile=leafRoof(leaf);
    if(!profile){
        if(const auto piece=shapedPiece(leaf)){
            const auto size=block.hi.x-block.lo.x;
            Box local{(region.lo-block.lo)/size,(region.hi-block.lo)/size};
            if(auto result=piece->occupied(local))
                return Box{result->lo*size+block.lo,result->hi*size+block.lo};
            return {};
        }
        return block;
    }
    const Vec anchor{double(root.x),0,double(root.z)};
    Box part{block.lo-anchor,block.hi-anchor};
    for(int axis:{0,2}){
        part.lo[axis]=std::max(part.lo[axis],region.lo[axis]-anchor[axis]);
        part.hi[axis]=std::min(part.hi[axis],region.hi[axis]-anchor[axis]);
    }
    if(!profile->occupied(part,std::max(floor,block.lo.y)))return {};
    return Box{part.lo+anchor,part.hi+anchor};
}

bool World::overlaps(Vec feet,double size,double height) const {
    const auto player=playerBox(feet,size,height);
    if(player.lo.x < -2048 || player.hi.x > 2048 || player.lo.z < -2048 ||
        player.hi.z > 2048 || player.lo.y<0 || player.hi.y>96)return true;
    return !visitMatter(player,[&](Leaf leaf){
        const auto solid=solidBounds(leaf,player,player.lo.y);
        return !solid || !player.intersects(*solid);
    });
}

double World::sweep(Vec feet,double size,int axis,double distance,double height) const {
    if(std::abs(distance)<=1e-12)return 0;
    const Box body=playerBox(feet,size,height);
    const double low=axis==1?0:-2048,high=axis==1?96:2048;
    distance=std::clamp(distance,low-body.lo[axis],high-body.hi[axis]);
    if(std::abs(distance)<=1e-12)return 0;
    Box area=body;
    if(distance>0)area.hi[axis]+=distance;
    else area.lo[axis]+=distance;
    const double epsilon=std::clamp(size*0.002,1e-10,1e-7);
    double allowed=distance;
    visitMatter(area,[&](Leaf leaf){
        const auto solid=solidBounds(leaf,area,area.lo.y);
        if(!solid)return true;
        const auto& block=*solid;
        if(distance>0)allowed=std::min(allowed,std::max(0.0,block.lo[axis]-body.hi[axis]-epsilon));
        else allowed=std::max(allowed,std::min(0.0,block.hi[axis]-body.lo[axis]+epsilon));
        return std::abs(allowed)>1e-12;
    });
    return allowed;
}

std::vector<Bounds> detailBounds(Vec point,int depth) {
    std::vector<Bounds> result;
    for (int level=1;level<=depth;++level) {
        const int n=subdivisions(level-1);
        const Cell at=floorCell(point*n);
        constexpr int radius=2;
        Bounds box{(at-Cell{radius,radius,radius})*4,(at+Cell{radius+1,radius+1,radius+1})*4};
        result.push_back(box);
    }
    return result;
}
