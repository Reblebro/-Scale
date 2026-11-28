#include "../src/world.hpp"
#include <iostream>
#include <stdexcept>
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){
    const auto dirt=[](Cell p){return p.y<10?int(Dirt):int(Air);};
    const auto allow=[](Leaf){return true;};
    for(int tier:{0,1,3,FineDepth}){
        World w{std::function<int(Cell)>(dirt)};
        const int detail=std::min(tier+1,FineDepth);
        const int count=tier==detail?1:64;
        const double size=1.0/subdivisions(tier);
        const Vec point{-.125,10,.5};
        const auto rev=w.revision();
        require(w.wholeStroke(point,tier,Dirt,false,FullBlock-1,allow)==0 && w.revision()==rev,"fractional budget is refused without edits");
        require(w.wholeStroke(point,tier,Dirt,false,FullBlock,allow)==FullBlock,"whole dig");
        require(w.edits().size()==std::size_t(count),"one unit has the expected number of subcells");
        MatterVolume removed=0;
        for(const auto& e:w.edits()){
            require(e.level==detail && e.id==Air,"whole removal edits");
            require(!w.sample(detail,e.position).amount,"each dug cell is empty");
            removed+=volume(e.level);
        }
        require(removed==volume(tier),"exact logical volume at each tier");
        const Vec pile=point+Vec{4*size,0,0};
        const auto old=w.edits().size();
        require(w.wholeStroke(pile,tier,Dirt,true,FullBlock,allow)==FullBlock,"whole placement");
        require(w.edits().size()-old==std::size_t(count),"one deposited unit");
        for(std::size_t i=old;i<w.edits().size();++i){
            const auto& e=w.edits()[i];require(w.sample(e.level,e.position).amount==volume(e.level),"placed cell is full");
        }
        World replay{std::function<int(Cell)>(dirt)};
        for(const auto& [chunk,edits]:w.saveChunks())for(const auto& e:edits)replay.apply(e);
        for(const auto& e:w.edits())require(replay.sample(e.level,e.position).amount==w.sample(e.level,e.position).amount,"save replay");
    }
    World fragment{std::function<int(Cell)>([](Cell){return int(Air);})};
    fragment.apply({1,{0,40,0},Dirt});
    const auto rev=fragment.revision();
    require(!fragment.wholeStroke({.125,10.25,.125},0,Dirt,false,FullBlock,allow) && fragment.revision()==rev,"insufficient material leaves fragment intact");
    World protectedWorld{std::function<int(Cell)>(dirt)};
    require(!protectedWorld.wholeStroke({0,10,0},0,Dirt,false,FullBlock,[](Leaf){return false;}) && !protectedWorld.revision(),"protected area is unchanged");
    World mixed{std::function<int(Cell)>([](Cell p){return p.y==9 && p.x==0 && p.z==0?int(Dirt):int(Stone);})};
    require(mixed.wholeStroke({.5,10,.5},0,Dirt,false,FullBlock,allow)==FullBlock && !mixed.sample(0,{0,9,0}).amount && mixed.sample(0,{1,9,0}).id==Stone,"isolated whole deposit, adjacent material preserved");
    std::cout<<"PASS: whole-volume dig/place, tiers 0/1/3/8, protected and incomplete strokes, save replay\n";
}
