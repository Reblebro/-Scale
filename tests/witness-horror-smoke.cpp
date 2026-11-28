#include "../src/witness-horror.hpp"
#include <cassert>
#include <iostream>
#include <numeric>

int main(){
    std::array<std::int64_t,ItemLimit> bag{};
    std::uint64_t seed=1329;
    bag[PulseRounds]=6*FullBlock;
    std::array<int,ItemLimit> counts{};
    for(int i=0;i<10000;i++){
        bag[PulseRounds]-=FullBlock;
        const auto before=bag;
        const int item=dread::reward(bag,seed);
        assert(item!=Air);++counts[item];
        for(int j=0;j<ItemLimit;j++)assert(bag[j]-before[j]==((j==item || j==PulseRounds)?FullBlock:0));
        assert(bag[PulseRounds]==6*FullBlock);
    }
    int diversity=0;
    for(auto n:counts)if(n){assert(n>850 && n<1150);++diversity;}
    assert(diversity==10);
    // Full stacks are not selected; a full bag still refunds the round.
    bag.fill(999999LL*FullBlock);bag[Quartz]-=FullBlock;bag[PulseRounds]-=FullBlock;
    assert(dread::reward(bag,seed)==Quartz);
    bag[PulseRounds]-=FullBlock;
    assert(dread::reward(bag,seed)==Air && bag[PulseRounds]==999999LL*FullBlock);
    assert(dread::quietTarget(10)==0 && dread::quietTarget(31)==1);
    double gain=1;
    for(int i=0;i<180;i++)gain=dread::quietStep(gain,0,1./60);
    assert(gain<.0001);
    for(int i=0;i<600;i++)gain=dread::quietStep(gain,1,1./60);
    assert(gain>.999);
    dread::Event event;
    event.notice({1,2,3});assert(event.interesting());event.tick(31);assert(!event.interesting());
    event.enter(dread::Mode::Feed);assert(event.age==0);
    int last=0;
    for(int i=0;i<int(dread::FeedDuration*100);i++){
        const int pieces=dread::eaten(i*.01);
        assert(pieces>=last && pieces<=dread::Pieces);last=pieces;
    }
    assert(last==6);
    event.finish(35);assert(event.mode==dread::Mode::Watch && event.prey.kind==dread::Kind::None && event.cooldown==35);
    for(int id=0;id<6;id++){
        double normal=0,mimic=0,difference=0;
        for(int i=0;i<24000;i++){
            const double t=i/24000.;
            const double a=dread::voice(t,id,false),b=dread::voice(t,id,true);
            assert(std::isfinite(a) && std::isfinite(b) && std::abs(a)<.4 && std::abs(b)<.4);
            normal+=a*a;mimic+=b*b;difference+=(a-b)*(a-b);
        }
        assert(normal>1 && mimic>1 && difference>1);
    }
    std::cout<<"10,000 whole-resource exchanges, cap behavior, gradual silence/recovery, feeding timing, and six distinct voice signals passed.\n";
}
