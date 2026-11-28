#pragma once

#include "math.hpp"
#include <optional>

namespace contact {

// Geometry queries are in world coordinates. An attachment stores a point and normal
// in the collider's own frame so hinged or moved geometry can carry a planted limb.
struct Attachment {
    Cell cell{};
    int kind{};
    Vec point{}, normal{};
};
struct Surface {
    Vec point{}, normal{};
    std::optional<Attachment> attachment{};
};
struct Queries {
    std::function<std::optional<Surface>(Vec,Vec,double)> cast;
    std::function<bool(Surface&)> follow;
};
struct Chain {
    Vec root{}, joint{}, tip{}, normal{0,1,0};
    Vec forward{0,0,1};
    bool planted{};
    double pad{},grip{};
};
struct Pose {
    Vec pelvis{}, chest{}, neck{};
    std::array<Chain,2> legs{}, arms{};
};
struct Bones {
    double upper, lower;
    double minimumAngle, maximumAngle; // Interior angle, radians.
};
struct Anatomy {
    Bones leg{1.15,1.08,0.30,2.88}, arm{1.34,1.43,0.29,2.90};
    double hipWidth=0.23, shoulderWidth=0.52;
    double anklePad=0.09, palmPad=0.078;
    double hipHeight=2.03, chestHeight=3.69, headHeight=4.52;
};
struct Frame {
    Vec origin{};
    double scale=1, heading{};
    Vec point(Vec local) const;
    Vec local(Vec point) const;
    Vec vector(Vec local) const;
    Vec localVector(Vec world) const;
};
enum class Phase { Free, Swing, Stance };
struct Limb {
    Phase phase=Phase::Free;
    Surface support{};
    Vec start{}, tip{}, normal{0,1,0}, bend{}, forward{0,0,1};
    Vec restOffset{};
    double progress{}, duration{}, retry{}, restCrouch{};
};
struct Rig {
    Pose pose{};
    Anatomy anatomy{};
    bool ready{};
    Frame frame{};
    std::array<Limb,4> limbs{}; // left/right foot followed by left/right hand
    double crouch{}, climb{}, roll{}, idleClock{}, idleWeight{};
    Vec compensation{}, velocity{}, peekOffset{}; // world-space direction, in body units
    Vec peekBlend{};
    bool feeding{};
    Vec feedingTarget{};
    double feedBlend{},feedingGrip{},feedingEffort{},feedWeight{};

    void update(const Frame& next,bool low,bool climbing,bool falling,Vec climbDirection,
        double headroom,double dt,const Queries& scene);
};

Chain solveChain(Vec root,Vec target,Vec pole,Bones bones);
// Capsule clearance of distinct animated chains, in body-space meters.
// Negative means the mesh envelopes overlap; attachment seams are exempt.
double limbClearance(const Chain& a,bool armA,const Chain& b,bool armB);
}
