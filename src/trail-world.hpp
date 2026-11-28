#pragma once
#include "natural-world.hpp"
#include "world.hpp"

namespace natural {
// Both the post mesh and its light use the same actual terrain sample.
inline std::vector<Vec> groundedTrail(const World& world,int tx,int tz){
    std::vector<Vec> result;
    if(!world.seed || !world.sculptedTerrain)return result;
    for(Vec p:trailLanterns(tx,tz,*world.seed)){
        const Cell column{int(std::floor(p.x)),0,int(std::floor(p.z))};
        const auto* roof=world.roof(column);
        if(!roof)continue;
        p.y=roof->height(p.x-column.x,p.z-column.z);
        const Cell base=column+Cell{0,int(std::ceil(p.y))-1,0};
        if(world.altered(base) || !world.sample(0,base).solid())continue;
        // Use the real sloped roof, not the enclosing unit cube. A slight
        // allowance at the foot lets the pole embed into the terrain.
        if(!world.overlaps(p+Vec{0,.18,0},.90,2.24/.90))result.push_back(p);
    }
    return result;
}
}
