#pragma once
#include "micro.hpp"
#include <bitset>

namespace expedition {
constexpr int Hostiles=20;
constexpr double BatteryCapacity=1000;
constexpr int Village=4;
struct Creature {
    Vec feet{};
    float health=60;
    double yaw{},stride{},attack{};
};
class Wildlife {
    std::array<int,micro::Width*micro::Depth> ground{},distance{};
    std::uint64_t revision=~std::uint64_t{};
    Cell destination{-1,-1,-1};
    void survey(const micro::Area& area);
    void route(Cell goal);
public:
    std::array<Creature,Hostiles> enemies{};
    std::array<Creature,4> elves{};
    std::bitset<Hostiles> defeated;
    std::bitset<4> missingElves;
    bool initialized{};
    void prepare(const micro::Area& area);
    int advance(const micro::Area& area,Vec player,double size,double dt,std::optional<Vec> predator={});
    bool hit(Vec eye,Vec ray,double reach,double& distance,int damage);
};

void humanoid(Geometry& model,bool elf,int variant);
void villageRoof(Geometry& model,Vec cell,double ridge,bool detailed);
Vec villageTreeCenter(int index);
void villageTree(Geometry& model,int index,int family,bool detailed);
void villageStreet(Geometry& model);
Box furnitureVolume(int id,Vec origin,bool folded);
std::optional<Hit> furnitureRay(Vec start,Vec dir,double reach,const std::function<int(Cell)>& materialAt);
void decoration(Geometry& model,int id,Vec local,bool folded);
void pulseSidearm(Geometry& shell,Geometry& quartz,Geometry& cartridge,Geometry& flash);
}
