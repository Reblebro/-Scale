#pragma once
#include "math.hpp"

namespace mobility {
inline constexpr double OldSurfaceZoom=6.010964;
inline constexpr double OldRealmZoom=4.304;
inline constexpr double PreviousSurfaceZoom=OldSurfaceZoom+1.5;
inline constexpr double PreviousRealmZoom=OldRealmZoom+2.397049199842863;
inline constexpr int ExtraSteps=10;
inline constexpr double SurfaceStep=0.27,RealmStep=0.29;
inline constexpr double SurfaceZoom=PreviousSurfaceZoom+ExtraSteps*SurfaceStep;
inline constexpr double RealmZoom=PreviousRealmZoom+ExtraSteps*RealmStep;

inline int surfaceDetail(double zoom){
    if(zoom>=9.1)return 8;
    if(zoom>=8.0)return 7;
    if(zoom>=6.4)return 6;
    if(zoom>=5.1)return 5;
    return std::clamp(int(std::floor(zoom+0.60)),0,4);
}

inline int realmDetail(double zoom){
    return std::clamp(int(std::floor(zoom*0.625769+0.12)),0,6);
}

inline double speed(double size,bool realm,bool flying,bool crouched,bool brisk){
    const double normal=flying?(realm?7.5:10.81):(realm?4.8:4.7);
    const double floor=realm?(flying?1.75:0.46):(flying?0.026:0.0068);
    const double assist=std::clamp((0.19-size)/0.19,0.0,1.0);
    const double walking=normal*size*0.53;
    return (brisk?std::hypot(normal*size,floor*assist):walking)*(crouched?0.54:1.0);
}

inline double tierSize(double tier){return std::pow(0.25,tier);}

inline double scaledSpeed(double size,bool realm,bool flying,bool crouched,bool brisk){
    const double normal=flying?(realm?7.5:10.81):(realm?4.8:4.7);
    return normal*size*(brisk?1.0:0.53)*(crouched?0.54:1.0);
}

template<class Fits>
int fitTier(int current,int desired,const Fits& fits){
    if(desired>=current)return desired;
    while(current>desired && fits(current-1))--current;
    return current;
}

struct Vertical {
    double velocity{},grace{},queued{};
    void reset(){velocity=grace=queued=0;}
};

template<class Sweep>
void advance(Vec& position,double size,Vec move,double dt,double stepHeight,double fallScale,
             bool flying,bool jump,bool& previousJump,bool& grounded,Vertical& vertical,const Sweep& sweep){
    const double epsilon=std::max(3e-10,size*2e-6);
    if(flying){
        for(int axis:{0,2,1})
            position[axis]+=sweep(position,size,axis,move[axis]);
        vertical.reset();
        grounded=false;
        previousJump=jump;
        return;
    }

    vertical.queued=jump&&!previousJump?0.11:std::max(0.0,vertical.queued-dt);
    vertical.grace=grounded?0.075:std::max(0.0,vertical.grace-dt);
    previousJump=jump;
    const bool jumped=vertical.queued>0 && vertical.grace>0;
    if(jumped){
        vertical.reset();
        vertical.velocity=5.9;
        grounded=false;
    }
    const int lead=std::abs(move.x)>=std::abs(move.z)?0:2;
    for(int axis:{lead,2-lead}){
        if(std::abs(move[axis])<=1e-12)continue;
        double distance=sweep(position,size,axis,move[axis]);
        if(grounded && std::abs(distance-move[axis])>epsilon){
            const double rise=sweep(position,size,1,stepHeight);
            Vec upper=position+Vec{0,rise,0};
            const double across=sweep(upper,size,axis,move[axis]);
            if(rise>epsilon && std::abs(across)>std::abs(distance)+epsilon){
                upper[axis]+=across;
                const double probe=rise+std::max(size*0.1,epsilon*3);
                const double down=sweep(upper,size,1,-probe);
                if(down>-probe+epsilon){
                    position.y=upper.y+down;
                    distance=across;
                }
            }
        }
        position[axis]+=distance;
    }

    if(grounded && !jumped){
        const double snap=std::max(epsilon*3,std::min(stepHeight*0.56,fallScale*0.25));
        const double down=sweep(position,size,1,-snap);
        if(down>-snap+epsilon){
            position.y+=down;
            vertical.velocity=0;
            return;
        }
        grounded=false;
    }
    vertical.velocity=std::max(-20.0,vertical.velocity-dt*15.0);
    const double desired=vertical.velocity*fallScale*dt;
    const double actual=sweep(position,size,1,desired);
    position.y+=actual;
    const bool contact=std::abs(actual-desired)>epsilon;
    grounded=contact && desired<0;
    if(contact)vertical.velocity=0;
}
}
