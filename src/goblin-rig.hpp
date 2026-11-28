#pragma once
#include "render.hpp"

namespace settlement {
// Rigid armor is bound to its owning joint. Flexible pieces have overlapping
// cuffs at each joint, so no whole-character rocking or detached hands.
class GoblinRig {
    struct Bone { int parent; Vec pivot; Geometry mesh; };
    std::vector<Bone> bones;
    int pelvis{},spine{},chest{},neck{},head{},scarf{},tabard{},role{};
    Geometry armament;
    struct Side {int shoulder,upper,elbow,wrist,hip,knee,ankle,toe; std::array<std::array<int,3>,5> digit;};
    std::array<Side,2> sides{};
    int add(int parent,Vec pivot);
public:
    void build(bool distant,int weaponClass=0);
    void pose(Geometry& out,double phase,double motion,double idle,double collapse=0,
              double look=0,double attack=-1,int piece=-1,double impact=0) const;
    std::size_t jointCount() const {return bones.size();}
};
}
