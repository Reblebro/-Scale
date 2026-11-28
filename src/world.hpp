#pragma once
#include "math.hpp"
#include "terrain-profile.hpp"
#include "terrain-piece.hpp"
#include <unordered_set>
#include <bitset>
#include <array>
#include <filesystem>
#include <memory>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>
#include <future>

enum Material : std::uint8_t {
    Air, Grass, Dirt, Stone, CopperOre, IronOre, Coal, Lodestone, Timber, Leaves, Clay,
    CampTile, Sand, Quartz, Water, Bedrock, Pick, Kiln, Copper, Iron, Coil, Crank, Magnet,
    Generator, Cable, Lamp, Switch, Socket, Enamel, Steel, SampleCopper, SampleIron, IronPick, Quarry, North, Fabricator, PulsePistol, PulseRounds, QuartzSight, PowerPack, Door, Seat, Lantern, Plank, RoofTile, OpenDoor, SlopedPlank, SlopedTile, PrismPress, PulseCrystal, Tempering, PulseRifle, ResourceWell, HouseBuilder, MeadowGate, QuartzGate, CrankGenerator, SolarPanel, ItemLimit
};
constexpr bool furniture(int id){return id>=Door && id<=SlopedTile;}
constexpr int furnitureBase(int id){
    return id==OpenDoor?Door:id==SlopedPlank?Plank:id==SlopedTile?RoofTile:id;
}
constexpr int alternateFurniture(int id) {
    switch(id){
        case Door:return OpenDoor;
        case OpenDoor:return Door;
        case Plank:return SlopedPlank;
        case SlopedPlank:return Plank;
        case RoofTile:return SlopedTile;
        case SlopedTile:return RoofTile;
        default:return id;
    }
}
const char* materialName(int id);
constexpr int FineDepth=8;
using MatterVolume=std::int64_t;
constexpr int subdivisions(int level) { return 1<<(2*level); }
constexpr MatterVolume volume(int level) { return MatterVolume{1}<<(6*(FineDepth-level)); }
constexpr MatterVolume FullVolume=volume(0);
// Inventory balances keep their original units across terrain precision upgrades.
constexpr std::int64_t FullBlock=std::int64_t{1}<<24;
constexpr double EyeHeight=1.41;
constexpr double PlayerHeight=1.50;
constexpr double CrouchHeight=0.91;
struct Sample {
    int id=0;
    MatterVolume amount=0;
    bool solid() const { return amount>0 && id!=Water; }
};
constexpr double ShovelRadius=0.6625; // in active-tier cells
constexpr bool sculptable(int id){return id>=Grass && id<=Quartz;}
using SoilHeights=std::array<std::int16_t,4>; // 1/256 of a leaf above its base
struct Edit { int level; Cell position; int id; std::optional<SoilHeights> surface{}; };
struct Leaf {
    Cell position;
    int level,material;
    Box bounds() const {
        const double side=1.0/subdivisions(level);
        const auto start=position.v()*side;
        return {start,start+Vec{side,side,side}};
    }
};
struct Partition { int material=Air; bool divided=false; };
struct BlockPattern { int material{}; std::uint64_t mask{}; bool refined{}; };

struct Node {
    int fill{};
    bool built{};
    std::shared_ptr<const TerrainRoof> surface;
    MatterVolume amount{};
    std::vector<std::pair<int,std::unique_ptr<Node>>> branches;
    Node(int id,MatterVolume capacity):fill(id),amount(id?capacity:0) {}
    Node* branch(int index);
    const Node* branch(int index) const;
    int representative() const;
};
using Tile=std::array<std::uint8_t,32*32*ForestWorldHeight>;
using Composition=std::array<MatterVolume,16>;
struct TerrainTile {
    Tile material;
    std::array<TerrainRoof,32*32> roofs;
    std::unordered_map<int,std::uint64_t> surface;
    std::bitset<32*32*ForestWorldHeight> livingTrees;
};

struct Hit { Cell cell,previous,normal; int level,id; double distance; Vec surfaceNormal{}; };

class World {
    std::filesystem::path data;
    std::function<int(Cell)> generated;
    std::unordered_map<Cell,TerrainTile,CellHash> tiles;
    std::unordered_map<Cell,std::unique_ptr<Node>,CellHash> modified;
    std::vector<Edit> changes;
    std::unordered_set<Cell,CellHash> disturbed;
    std::unordered_map<Cell,TerrainRoof,CellHash> localRoofs;
    std::unordered_map<Cell,std::uint64_t,CellHash> changedTiles,changedTreeTiles,changedRoots;
    // Workers own only immutable generation inputs and their output tile.
    std::map<Cell,std::future<TerrainTile>> pendingTiles;
    static TerrainTile generateTile(const std::filesystem::path& data,int x,int z,
                                    std::optional<std::uint64_t> seed,bool sculptedTerrain);
    std::uint64_t revision_{};
    Cell streamedTile{};
    int streamedRadius=-1;
    std::pair<int,std::uint64_t> base(Cell pos) const;
    const TerrainRoof* rawRoof(Cell column) const;
    std::optional<Box> solidBounds(Leaf leaf,Box region,double floor) const;
    const Node* edited(Cell pos,int level,int& fallback) const;
    void count(const Node& current,int level,Composition& output) const;
    MatterVolume write(Node& current,int layer,const Edit& edit,bool profile);
    bool visit(Cell pos,int level,Box query,const std::function<bool(Leaf)>& visitor) const;
public:
    std::optional<std::uint64_t> seed;
    bool sculptedTerrain{};
    int latticeFrom=64;
    void setSeed(std::uint64_t value){seed=value;}
    int heightLimit() const {return seed && sculptedTerrain && !generated?ForestWorldHeight:ArchiveHeight;}
    explicit World(std::filesystem::path directory):data(std::move(directory)) {}
    explicit World(std::function<int(Cell)> baseMaterial):generated(std::move(baseMaterial)) {}
    bool validAssets() const;
    const Tile& require(int x,int z);
    const TerrainRoof* roof(Cell column) const;
    bool naturalTree(Cell root) const;
    int uneditedTreeMaterial(Cell root) const;
    bool treeThroughRoof(Cell root) const;
    const TerrainRoof* leafRoof(Leaf leaf) const;
    bool altered(Cell root) const { return modified.contains(root); }
    void defineRoof(Cell column,TerrainRoof profile);
    std::optional<TerrainStone> shapedPiece(Leaf leaf) const;
    void stream(Cell center,int radius);
    void streamInteractive(Cell center,int radius);
    bool tileReady(int x,int z) const {return x< -64 || x>=64 || z< -64 || z>=64 || tiles.contains({x,0,z});}
    std::uint64_t treeVersion(Cell tile) const;
    void clearEdits();
    Sample sample(int level,Cell pos) const;
    Composition composition(int level,Cell pos) const;
    MatterVolume apply(Edit edit,bool record=true);
    Partition partition(int level,Cell pos) const;
    BlockPattern pattern(Cell root) const;
    bool editedChildren(Cell root) const;
    // Returns the inventory units actually transferred by a single stroke.
    std::int64_t shovel(Vec point,int tier,int material,bool deposit,std::int64_t budget,
                        const std::function<bool(Leaf)>& allowed,Vec normal={0,1,0});
    // One inventory unit = one cell at the active tier, assembled from 64
    // equal subcells (one cell at FineDepth). No edits on an incomplete stroke.
    std::int64_t wholeStroke(Vec point,int tier,int material,bool deposit,std::int64_t budget,
                            const std::function<bool(Leaf)>& allowed,Vec normal={0,1,0});
    bool visitMatter(Box bounds,const std::function<bool(Leaf)>& visitor) const;
    void visitFaceNeighbors(Leaf voxel,int side,const std::function<void(Leaf,Leaf)>& visitor) const;
    void visibleFace(Leaf voxel,int side,const std::function<void(Leaf)>& visible) const;
    std::optional<Hit> ray(Vec origin,Vec direction,int level,double reach) const;
    static Box playerBox(Vec feet,double size,double height=PlayerHeight);
    bool overlaps(Vec feet,double size,double height=PlayerHeight) const;
    double sweep(Vec feet,double size,int axis,double distance,double height=PlayerHeight) const;
    const auto& edits() const { return changes; }
    std::map<Cell,std::vector<Edit>> saveChunks() const;
    auto revision() const { return revision_; }
    std::uint64_t meshVersion(Cell tile) const;
    std::uint64_t detailVersion(Bounds area,int level) const;
};

int harvestMaterial(int id);
bool protectedMaterial(int id);
bool requiresPick(int id);
std::vector<Bounds> detailBounds(Vec point,int depth);
