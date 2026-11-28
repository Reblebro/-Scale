#pragma once

#include "render.hpp"
#include "contact-rig.hpp"
#include <filesystem>

void northSound();
void northGeometry(Geometry& geometry,bool proxy);
void witnessHead(Geometry& shape,Geometry* movingJaw=nullptr);
Vec witnessSocket(int side);
void witnessEye(Geometry& shape,bool clouded);
void witnessSound(double pan,double strength);
void forestAudio(bool enabled,double gain,bool night);
void loadCreatureVoices(const std::filesystem::path& assets);
void creatureVoice(int identity,bool imitation,double pan,double strength,bool speech=false);

// Each rigid section is built once in bone space. The same posed triangles are
// used by the renderer and by the shot query, including the hands and mask.
class WitnessSkin {
    Geometry trunk, neck;
    bool proxy{};
    std::array<Geometry,2> hands,feet,sleeves;
    struct Finger {std::array<Vec,3> pivots{};std::array<Geometry,3> segments;};
    std::array<std::array<Finger,5>,2> fingers;
public:
    void build(bool distant);
    void pose(Geometry& mesh,const contact::Pose& pose) const;
};
