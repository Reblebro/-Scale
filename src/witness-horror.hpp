#pragma once
#include "world.hpp"

namespace dread {
// All durations are in real seconds, not the world's scale-dependent clock.
enum class Mode { Watch, Hunt, Feed, Retreat, Concealed, Reappear, Inspect };
enum class Kind { None, Deer, Crawler, Elf, Goblin };
struct Prey {
    Kind kind=Kind::None;
    Cell tile{};
    int index=-1;
    Vec feet{};
    double yaw{};
    int voice() const {return kind==Kind::Elf?index:kind==Kind::Deer?4:5;}
};
constexpr int Pieces=6;
struct Dropping {Vec from{},to{};bool ready{};};
struct Event {
    Mode mode=Mode::Watch;
    Prey prey{};
    Vec goal{},interest{},retreatOrigin{};
    double age{},engageAge{},cooldown=18,interestAge=1000,scan{},mimicTimer=12;
    bool interestKnown=false;
    std::array<Dropping,Pieces> droppings{};
    void enter(Mode next){mode=next;age=0;engageAge=0;if(next==Mode::Feed)droppings={};}
    void finish(double delay){enter(Mode::Watch);cooldown=delay;prey={};}
    void tick(double dt){age+=dt;cooldown=std::max(0.,cooldown-dt);interestAge+=dt;scan=std::max(0.,scan-dt);mimicTimer=std::max(0.,mimicTimer-dt);}
    void notice(Vec point){interest=point;interestAge=0;interestKnown=true;}
    bool interesting() const {return interestKnown && interestAge<30;}
};
inline double horizontal(Vec p){return std::hypot(p.x,p.z);}
inline double quietTarget(double range){const double t=std::clamp((range-12.)/18.,0.,1.);return t*t*(3-2*t);}
inline double quietStep(double previous,double target,double dt){return std::lerp(previous,target,-std::expm1(-dt*(target<previous?3.5:.8)));}
// A shared animation clock drives grasp, lift, jaw motion and release.
constexpr double FallDuration=2.05, BiteDuration=1.90;
constexpr double FeedDuration=FallDuration+Pieces*BiteDuration+.85;
constexpr double ReleasePhase=.79;
inline double smooth(double x){x=std::clamp(x,0.,1.);return x*x*(3-2*x);}
inline double soft(double x){x=std::clamp(x,0.,1.);return x*x*x*(10+x*(-15+6*x));}
struct Key {double t,value;};
// Shape-preserving Hermite interpolation. Adjacent segments share a tangent;
// extrema have zero velocity, and no segment overshoots its authored range.
template<std::size_t N> inline double curve(const std::array<Key,N>& keys,double t){
    if(t<=keys.front().t)return keys.front().value;
    if(t>=keys.back().t)return keys.back().value;
    const auto tangent=[&](std::size_t i){
        if(i==0 || i+1==N)return 0.;
        const double a=(keys[i].value-keys[i-1].value)/(keys[i].t-keys[i-1].t);
        const double b=(keys[i+1].value-keys[i].value)/(keys[i+1].t-keys[i].t);
        return a*b>0?2*a*b/(a+b):0.;
    };
    for(std::size_t i=1;i<N;++i)if(t<=keys[i].t){
        const auto a=keys[i-1],b=keys[i];const double d=b.t-a.t,u=(t-a.t)/d;
        return (2*u*u*u-3*u*u+1)*a.value+(u*u*u-2*u*u+u)*d*tangent(i-1)
            +(-2*u*u*u+3*u*u)*b.value+(u*u*u-u*u)*d*tangent(i);
    }
    return keys.back().value;
}
inline int eaten(double age){return std::clamp(int(std::max(0.,age-FallDuration)/BiteDuration),0,Pieces);}
inline double bitePhase(double age){return std::clamp((age-FallDuration-eaten(age)*BiteDuration)/BiteDuration,0.,1.);}
inline double biteProgress(double age){return std::clamp(bitePhase(age)/.76,0.,1.);}
inline double grasp(double p){return soft((p-.10)/.17);}
inline double lift(double p){return curve(std::array<Key,7>{{{0,0},{.25,0},{.34,.14},{.46,.66},{.56,1},{.83,1},{1,1}}},p);}
inline double lowerHand(double p){return soft((p-.80)/.20);}
inline double swallow(double p){return soft((p-.62)/.18);}
inline double fallAngle(double t){return curve(std::array<Key,12>{{{0,0},{.05,.035},{.11,-.06},{.20,-.18},{.31,-.43},{.43,-.76},{.57,-1.18},{.68,-1.50},{.76,-1.43},{.84,-1.52},{.93,-1.49},{1,-1.50}}},t);}
inline double supportHeight(const auto& vertices,double pitch){
    double lo=1e9;for(const auto& v:vertices)lo=std::min(lo,v.y*std::cos(pitch)+v.z*std::sin(pitch));
    return vertices.empty()?0.:-lo;
}
// Anticipation, controlled opening, three asymmetric closures, recovery.
inline double jawAngle(double age){
    if(age<FallDuration || eaten(age)>=Pieces)return 0.;
    const double p=bitePhase(age);
    return curve(std::array<Key,15>{{{0,0},{.18,0},{.30,-.035},{.40,-.19},{.52,-.37},{.56,-.37},
        {.62,.135},{.65,.10},{.69,-.17},{.73,.045},{.76,-.076},{.79,.045},{.83,-.018},{.92,0},{1,0}}},p);
}
inline double fallSlide(double t){return .29*soft((t-.12)/.66);}
inline double impactSpring(double t,double contact,double frequency,double damping){
    const double elapsed=std::max(0.,t-contact);
    return std::sin(elapsed*frequency)*std::exp(-elapsed*damping)*(1-soft((t-.86)/.14));
}
inline double feedPull(double p){return lift(p)*(1-lowerHand(p));}
inline double feedSwallow(double p){
    return soft((p-.78)/.07)*(1-soft((p-.86)/.13));
}
inline double releaseTime(int piece){return FallDuration+(piece+ReleasePhase)*BiteDuration;}

inline std::uint64_t random(std::uint64_t& state){
    auto v=(state+=0x9e3779b97f4a7c15ULL);
    v=(v^(v>>30))*0xbf58476d1ce4e5b9ULL;v=(v^(v>>27))*0x94d049bb133111ebULL;
    return v^(v>>31);
}
inline int reward(std::array<std::int64_t,ItemLimit>& bag,std::uint64_t& state){
    constexpr std::array<int,10> pool={Dirt,Stone,Timber,Clay,CopperOre,IronOre,Coal,Lodestone,Sand,Quartz};
    constexpr auto cap=999999LL*FullBlock;
    std::array<int,pool.size()> available{};std::size_t count=0;
    for(int id:pool)if(bag[id]<=cap-FullBlock)available[count++]=id;
    // The caller has already deducted one round. Refund exactly that round,
    // even when all resource stacks are at their maximum.
    bag[PulseRounds]+=FullBlock;
    if(!count)return Air;
    const int id=available[random(state)%count];bag[id]+=FullBlock;return id;
}
// The same six identities are used for living calls and imitations. These are
// intentionally non-verbal formant calls rather than pretend speech samples.
inline double voice(double time,int identity,bool imitation){
    identity=std::clamp(identity,0,5);
    const double t=time*(imitation?.87:1.0);
    if(t<0 || t>=1.24)return 0;
    const int syllable=std::min(2,int(t/.42));
    const double u=t-.42*syllable;
    const double env=std::sin(Pi*std::clamp(u/.36,0.,1.));
    const double base=std::array<double,6>{162,228,181,204,310,87}[identity];
    const double f=base*(1.+.11*((identity+syllable*2)%5-2));
    const double phase=2*Pi*(f*u+7*u*u+(imitation?.16*std::sin(u*31):0));
    const double fundamental=std::sin(phase)+.34*std::sin(2*phase)+.19*std::sin(3*phase);
    return env*env*(.095*fundamental+.043*std::sin(phase*4.03)+.025*std::sin(phase*6.02));
}
}
