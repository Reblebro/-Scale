#include "../src/goblin-rig.hpp"
#include "../src/settlement.hpp"
#include "../src/witness-horror.hpp"
#include <cassert>
#include <iostream>
#include "../src/platform.hpp"
#include <stdexcept>
int main(int argc,char** argv){
    int deer=0;
    for(unsigned i=0;i<100000;i++)deer+=settlement::rareDeer(mix(i));
    assert(deer>1200 && deer<1800);
    std::array<std::size_t,3> counts{};
    for(int role=0;role<3;role++){
        settlement::GoblinRig rig;rig.build(true,role);assert(rig.jointCount()==53);
        Geometry standing,posed;rig.pose(standing,0,0,0);counts[role]=standing.points.size();
        for(int f=0;f<=20;f++){
            rig.pose(posed,.3,0,.5,0,.35,f/20.);
            assert(posed.points.size()==standing.points.size());
            for(const auto& v:posed.points){assert(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z));assert(std::abs(length({v.nx,v.ny,v.nz})-1)<.003);}
        }
        rig.pose(posed,0,0,0,1);std::size_t total=0;
        for(int part=0;part<dread::Pieces;part++){Geometry g;rig.pose(g,0,0,0,1,0,-1,part);assert(!g.points.empty());total+=g.points.size();}
        assert(total<posed.points.size()); // weapon intentionally excluded from anatomical parts
        std::cout<<"Role "<<role<<": "<<counts[role]/3<<" triangles; six rig-derived parts, 21 attack poses.\n";
    }
    assert(counts[0]!=counts[1]&&counts[1]!=counts[2]);
    dread::Event event;event.enter(dread::Mode::Hunt);event.engageAge=1;event.enter(dread::Mode::Feed);assert(event.engageAge==0);
    std::cout<<"Rare deer distribution: "<<deer<<" / 100000 tile hashes.\n";
    if(argc>1){
        if(SDL_Init(platform::InitVideo))throw std::runtime_error(SDL_GetError());
        SDL_GL_SetAttribute(17,3);SDL_GL_SetAttribute(18,3);SDL_GL_SetAttribute(21,1);SDL_GL_SetAttribute(6,24);
        auto* w=SDL_CreateWindow("Goblin weapons",0,0,960,1000,platform::OpenGL|platform::Hidden);
        if(!w)throw std::runtime_error(SDL_GetError());auto ctx=SDL_GL_CreateContext(w);if(!ctx)throw std::runtime_error(SDL_GetError());
        {
            Renderer renderer("assets");Mesh mesh,floorMesh;Geometry ground;
            ground.box({-8,-.15,-8},{8,0,8},Stone,{.25f,.21f,.18f});floorMesh.upload(ground);
            for(int role=0;role<3;role++){
                settlement::GoblinRig rig;rig.build(false,role);Geometry posed;rig.pose(posed,0,0,0);mesh.upload(posed);
                View v;v.camera={.75,.94,2.9};v.forward=unit(Vec{0,.78,0}-v.camera);v.right=unit(cross(v.forward,{0,1,0}));v.up=cross(v.right,v.forward);v.ambient=.64f;v.sky={.055f,.045f,.04f};v.fog=50;v.far=60;v.skyMode=0;v.fov=2.5f;
                std::array<Light,8> lamps{};lamps[0]={{-.9,1.5,1.1},.40f,false};lamps[1]={{.8,1.6,-.4},.28f,false};
                renderer.start(v,960,1000,lamps);renderer.draw(floorMesh);renderer.draw(mesh);renderer.finishHud();renderer.settle();renderer.screenshot(std::filesystem::path(argv[1])/(std::to_string(role)+".ppm"));
            }
        }
        SDL_GL_DeleteContext(ctx);SDL_DestroyWindow(w);SDL_Quit();
    }
}
