#pragma once
#include "math.hpp"
#include <cmath>
#include <algorithm>

// A surface clock independent of the player's scale/time dilation. The
// original save file can carry the new clock as one optional line.
namespace solarcycle {
inline constexpr double CycleSeconds=720.0, InitialClock=CycleSeconds*10.0/24.0;
struct State { Vec sun; double day, twilight; bool night; };
inline double wrap(double seconds){
    double t=std::fmod(seconds,CycleSeconds);return t<0?t+CycleSeconds:t;
}
inline double smooth(double a,double b,double x){
    const double v=std::clamp((x-a)/(b-a),0.0,1.0);return v*v*(3-2*v);
}
inline State at(double seconds){
    const double angle=2*Pi*(wrap(seconds)/CycleSeconds-.25);
    const Vec sun=unit({.80*std::cos(angle),.85*std::sin(angle),.38*std::sin(angle)+.24});
    return {sun,smooth(-.16,.30,sun.y),smooth(-.18,.22,sun.y)*(1-smooth(.22,.57,sun.y)),sun.y<=0};
}
// Refresh the shadow map at a discrete solar angle, not every frame. Both
// sampling and rendering use the same quantized direction.
inline Vec shadowSun(double seconds){return at(std::floor(wrap(seconds)/5.0)*5.0).sun;}
}
