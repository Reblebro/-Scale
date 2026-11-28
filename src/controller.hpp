#pragma once

#include "platform.hpp"
#include <algorithm>
#include <array>
#include <cmath>

class Controller {
    SDL_GameController* pad{};
    std::array<bool,17> previous{},current{};
    SDL_GameController* open(){
        for(int i=0;i<SDL_NumJoysticks();i++)
            if(SDL_IsGameController(i))if(auto* device=SDL_GameControllerOpen(i))return device;
        return nullptr;
    }
public:
    enum Button { Confirm=0,Back=1,Interact=2,Inventory=3,Map=4,Pause=6,Brisk=7,Crouch=8,
        Grow=9,Shrink=10,Up=11,Down=12,Left=13,Right=14,Aim=15,Fire=16 };
    Controller(){if(SDL_InitSubSystem(0x2000)==0)pad=open();}
    ~Controller(){if(pad)SDL_GameControllerClose(pad);}
    Controller(const Controller&)=delete;
    Controller& operator=(const Controller&)=delete;
    void reconnect(){
        if(pad && SDL_GameControllerGetAttached(pad))return;
        if(pad)SDL_GameControllerClose(pad);
        pad=open();
        previous.fill(false);current.fill(false);
    }
    double axis(int index) const {
        if(!pad)return 0;
        const double raw=double(SDL_GameControllerGetAxis(pad,index))/32768;
        constexpr double deadZone=0.17;
        return std::copysign(std::max(0.0,(std::abs(raw)-deadZone)/(1-deadZone)),raw);
    }
    void sample(bool focused){
        previous=current;
        current.fill(false);
        if(!pad || !focused)return;
        for(int i=0;i<15;i++)current[i]=SDL_GameControllerGetButton(pad,i)!=0;
        current[Aim]=axis(4)>0.28;
        current[Fire]=axis(5)>0.28;
    }
    bool held(Button button) const{return current[button];}
    bool pressed(Button button) const{return current[button] && !previous[button];}
};
