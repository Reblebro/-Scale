#pragma once

#include "math.hpp"
#include "world.hpp"

class Geometry;

namespace settlement {
struct Site { Vec chest,guard; const char* name; };
inline constexpr Site Surface{{15.5,29,0.5},{11.3,29,2.5},"Reedwatch camp"};
inline constexpr Site Pocket{{38.5,5,16.5},{30.5,5,17.5},"Thimble camp"};
inline constexpr Site Vault{{42.5,2,35.5},{34.5,2,23.5},"Buried Goblin Hold"};

inline const Site* site(int layer){
    if(layer<0)return &Surface;
    if(layer==0 || layer==2 || layer==5)return &Pocket;
    if(layer==6)return &Vault;
    if(layer==7){static constexpr Site fort{{24.5,5.01,31.5},{10.5,8.01,16.5},"The Marrowkeep"};return &fort;}
    return nullptr;
}

template<class Write>
void surfaceColumn(int x,int z,Write write){
    if(x<9 || x>18 || z<-3 || z>5)return;
    write(28,Stone);
    for(int y=29;y<=35;y++)write(y,Air);
    const bool gate=(x==9 && (z==1 || z==2)) || (z==5 && (x==12 || x==13));
    if((x==9 || x==18 || z==-3 || z==5) && !gate){
        write(29,Timber);
        if((x+z)%3==0)write(30,Timber);
    }
    if(x>=14 && x<=16 && z>=-2 && z<=2){
        write(32,Clay);
        if(x==16 && z%2==0){write(29,Timber);write(30,Timber);write(31,Timber);}
    }
}

template<class Write>
void hiddenSite(int layer,Write write){
    if(!site(layer) || layer<0)return;
    if(layer==6){
        for(int z=12;z<=43;z++)for(int x=30;x<=46;x++){
            const bool gate=x==30 && z>=14 && z<=17;
            const bool edge=x==30 || x==46 || z==12 || z==43;
            write(Cell{x,1,z},Stone);
            for(int y=2;y<=6;y++){
                const bool column=(x==37 || x==44) && z%10==0;
                const bool divider=x==37 && z>=22 && z<=40 && (z<26 || z>29) && (z<35 || z>37);
                const bool alcove=x<=35 && z==33 && x!=33;
                write(Cell{x,y,z},(edge && !gate) || column || divider || alcove?Stone:y==6?Lodestone:Air);
            }
        }
        for(int z=14;z<=17;z++)for(int x=26;x<=31;x++){
            const int floor=x<=27?4:x==28?3:x==29?2:1;
            for(int y=2;y<8;y++)write(Cell{x,y,z},y<=floor?Stone:Air);
        }
        return;
    }
    for(int z=12;z<=22;z++)for(int x=27;x<=43;x++){
        write(Cell{x,4,z},Stone);
        for(int y=5;y<=13;y++)write(Cell{x,y,z},Air);
        const bool gate=z>=15 && z<=18 && (x==28 || x==42);
        if((x==28 || x==42 || z==12 || z==22) && !gate){
            write(Cell{x,5,z},Timber);
            if((x+z)%3==0)write(Cell{x,6,z},Timber);
        }
        if(x>=37 && x<=41 && z>=13 && z<=19){
            write(Cell{x,8,z},Clay);
            if(x==41 && z%2==0)for(int y=5;y<=7;y++)write(Cell{x,y,z},Timber);
        }
    }
}

// Coordinates in the dedicated final-scale room. The four spear slots
// are fixed tower sentries; the other slots are mobile courtyard troops.
inline constexpr int FortressGuards=15;
inline constexpr std::array<Vec,FortressGuards> FortressPosts={{{10.5,8.01,16.5},{37.5,8.01,16.5},{10.5,8.01,41.5},{37.5,8.01,41.5},
    {20.5,5.01,17.5},{28.5,5.01,17.5},{15.5,5.01,22.5},{33.5,5.01,22.5},{24.5,5.01,29.5},
    {12.5,5.01,20.5},{35.5,5.01,20.5},{12.5,5.01,30.5},{35.5,5.01,30.5},{16.5,5.01,39.5},{32.5,5.01,39.5}}};
inline constexpr std::array<Vec,2> Campfires={{{17.5,5,27.5},{31.5,5,27.5}}};
inline constexpr std::array<Vec,2> Defenses={{{20.5,7.3,15.5},{28.5,7.3,15.5}}};
inline constexpr Vec BroodCradle{24.5,5.40,38.5}, BroodExit{24.5,5.01,33.0};
inline constexpr double BroodDuration=120.;
inline constexpr int fortressClass(int slot){return slot<4?0:slot<9?2:1;}
inline constexpr std::array<int,6> GrowthOrder={1,4,5,2,3,0};
struct Brood {int slot=-1;double age{},clock{};std::array<double,2> defenseWait{};};

template<class Write> void fortressTerrain(Write write){
    // Solid masonry and walkable platforms, not visual-only walls.
    for(int z=14;z<=44;z++)for(int x=8;x<=39;x++){
        const bool edge=x==8 || x==39 || z==14 || z==44;
        const bool gate=z==14 && x>=22 && x<=26;
        if(edge && !gate){for(int y=5;y<=7;y++)write(Cell{x,y,z},y==6?Stone:IronOre);
            if((x+z)%2==0)write(Cell{x,8,z},Stone);}
        if(gate){write(Cell{x,8,z},Timber);write(Cell{x,9,z},Stone);}
    }
    for(int cx:{10,37})for(int cz:{16,41}){
        for(int z=cz-1;z<=cz+1;z++)for(int x=cx-1;x<=cx+1;x++){
            for(int y=5;y<=7;y++)write(Cell{x,y,z},y==7?Timber:Stone);
            const bool outer=(cx==10?x==cx-1:x==cx+1) || (cz==16?z==cz-1:z==cz+1);
            if(outer){write(Cell{x,8,z},Timber);if((x+z)%2==0)write(Cell{x,9,z},Timber);}
            // Inner side open so a tower spear has a real line of fire.
        }
    }
    // Raised bolt-launcher mounts flanking the entry, with clear firing arcs.
    for(int x:{20,28})for(int y=5;y<=6;y++)write(Cell{x,y,15},Stone);
    // Brood hut: open doorway, stone footings, timber walls, stepped roof.
    for(int z=34;z<=41;z++)for(int x=19;x<=29;x++){
        const bool edge=x==19 || x==29 || z==34 || z==41;
        const bool door=z==34 && x>=23 && x<=25;
        if(edge && !door)for(int y=5;y<=8;y++){
            if(y==7 && (x==19 || x==29) && (z==36 || z==39))continue;
            write(Cell{x,y,z},y==5?Stone:Timber);
        }
        if(door)write(Cell{x,8,z},Timber);
        const int roof=9+(5-std::abs(x-24))/2;
        write(Cell{x,roof,z},Clay);
        if(z==34 || z==41)for(int y=9;y<roof;y++)write(Cell{x,y,z},Timber);
    }
}
void fortressArt(Geometry& architecture,Geometry& fireBase,Geometry& flame);
inline constexpr double GuardScale=1.45;
inline constexpr int GuardClasses=3;
inline constexpr double DefenseRange=3.0;
inline constexpr double attackReach(int role){return role==1?9.0:role==2?2.35:3.0;}
inline constexpr double attackPeriod(int role){return role==1?1.65:role==2?.86:1.12;}
inline constexpr bool rareDeer(std::uint32_t hash){return hash%1000<15;}
struct Guard {
    Vec feet{};
    double yaw{},stride{},blockTime{},visualClock{},visualMotion{};
    int health=110;
    bool ready{},alert{},looted{},fallenToWitness{},defending{};
    double headTurn{},attackAge=-1,attackWait{},fallSpeed{};
    double deathPhase{},deathMotion{},deathIdle{},deathLook{},deathAttack=-1,deathImpact{};
    void beginCollapse(double impact=0){
        deathPhase=stride;deathMotion=visualMotion;deathIdle=visualClock;
        deathLook=headTurn;deathAttack=attackAge;deathImpact=std::clamp(impact,-1.,1.);
        visualClock=visualMotion=0;attackAge=-1;
    }
};

void goblin(Geometry& mesh,bool proxy);
void chest(Geometry& mesh,bool open);
}
