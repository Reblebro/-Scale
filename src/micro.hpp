#pragma once
#include "world.hpp"
#include "render.hpp"

namespace micro {
constexpr int Count=7; // original realms, retained for save compatibility
constexpr int Fortress=Count, Total=Count+1;
constexpr int Width=48,Height=22,Depth=58;
constexpr int TileWidth=12,TilesX=(Width+TileWidth-1)/TileWidth,TilesZ=(Depth+TileWidth-1)/TileWidth;
constexpr int TileCount=TilesX*TilesZ;
inline Bounds tileBounds(int index){
    const int x=(index%TilesX)*TileWidth,z=(index/TilesX)*TileWidth;
    return {{x,0,z},{std::min(x+TileWidth,Width),Height,std::min(z+TileWidth,Depth)}};
}
constexpr Vec Arrival{24.5,5.01,5.0};
constexpr Vec Descent{24.5,5.04,51.5};
constexpr std::array<Vec,2> Entrances={{{12.5,29.0,-6.5},{76.5,17.0,25.5}}};
struct Layer { const char* name; const char* landmarks; double cellMeters; int ground,ore; Color sky; };
const Layer& details(int level);
double parentScale(int level);
Vec anchor(int family,int level);
Vec toParent(int family,int level,Vec local);
bool within(Cell c);
struct TerrainMeshes {
    Geometry solid,close,distant,fixtures,fixtureProxy,exact,roof,roofProxy,trees,sprigs;
};
struct Area {
    int family{},level{};
    bool inhabited{};
    std::vector<std::uint8_t> voxels;
    std::unordered_map<Cell,int,CellHash> edits;
    std::uint64_t revision{};
    Area(int family,int level,std::optional<std::uint64_t> seed={},bool inhabited=false);
    Area(const Area&)=delete;
    Area& operator=(const Area&)=delete;
    int raw(Cell cell) const;
    int at(Cell cell) const;
    const World& blocks() const {return matter;}
    std::uint64_t tileVersion(int index) const {return tileVersions[index];}
    bool setDetail(int detail,Cell cell,int material,bool campMining=false,std::optional<SoilHeights> surface={});
    std::int64_t shovel(Vec point,int tier,int material,bool deposit,std::int64_t budget,Box player,Vec normal);
    bool set(Cell cell,int material);
    bool overlaps(Vec feet,double size,double height=PlayerHeight) const;
    double sweep(Vec feet,double size,int axis,double distance,double height=PlayerHeight) const;
    std::optional<Hit> ray(Vec from,Vec dir,double range,int detail=0) const;
    void drawMeshes(TerrainMeshes& geometry,Bounds portion) const;
private:
    World matter;
    std::array<std::uint64_t,TileCount> tileVersions{};
    void changed(Cell root);
};
// Parent-world geometry is scenery, never part of the local playable room.
// Clip each axis-aligned face against a padded room volume, preserving the
// outside rectangles (rather than hiding entire oversized parent cells).
void backdropFace(Geometry& geometry,Vec origin,double size,int side,int material,Color shade);
void gate(Geometry& geometry,bool atom=false);
}
