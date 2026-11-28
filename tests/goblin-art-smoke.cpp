#include "../src/goblin-rig.hpp"
#include "../src/platform.hpp"
#include "../src/world.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){
    settlement::GoblinRig rig,low;rig.build(false);low.build(true);
    Geometry full,proxy,posed;rig.pose(full,0,0,0);low.pose(proxy,0,0,0);
    assert(rig.jointCount()==53 && low.jointCount()==53);
    assert(full.points.size()>proxy.points.size());
    std::cout<<rig.jointCount()<<" joints; "<<full.points.size()/3<<" full triangles; "<<proxy.points.size()/3<<" distant triangles\n";
    for(int frame=0;frame<48;frame++){
        rig.pose(posed,frame*Pi/24,1,frame/24.);
        assert(posed.points.size()==full.points.size());double bottom=1e9;
        for(auto& v:posed.points){assert(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z));assert(std::abs(length({v.nx,v.ny,v.nz})-1)<.002);if(int(v.id)!=GoblinAmberTexture)bottom=std::min(bottom,double(v.y));}
        assert(bottom>-.17);
    }
    if(SDL_Init(platform::InitVideo))throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(17,3);SDL_GL_SetAttribute(18,3);SDL_GL_SetAttribute(21,1);SDL_GL_SetAttribute(6,24);
    auto* w=SDL_CreateWindow("Goblin character test",0,0,800,1000,platform::OpenGL|platform::Hidden);
    if(!w)throw std::runtime_error(SDL_GetError());
    auto ctx=SDL_GL_CreateContext(w);if(!ctx)throw std::runtime_error(SDL_GetError());
    {
        Renderer r("assets");Mesh m,g;Geometry floor;floor.box({-8,-.15,-8},{8,0,8},Stone,{.25f,.21f,.18f});g.upload(floor);
        auto capture=[&](Vec camera,Vec target,const char* name,double phase=0,double motion=0){
            rig.pose(posed,phase,motion,0);m.upload(posed);View v;v.camera=camera;v.forward=unit(target-camera);v.right=unit(cross(v.forward,{0,1,0}));v.up=cross(v.right,v.forward);v.ambient=.64f;v.sky={.045f,.035f,.030f};v.fog=50;v.far=60;v.skyMode=0;v.fov=2.5f;
            std::array<Light,8> lamps{};lamps[0]={{-.9,1.5,1.1},.40f,false};lamps[1]={{.8,1.6,-.4},.28f,false};
            r.start(v,800,1000,lamps);r.draw(g);r.draw(m);r.finishHud();r.settle();if(argc>1)r.screenshot(std::filesystem::path(argv[1])/name);
        };
        capture({.55,.91,2.7},{0,.81,0},"goblin-front.ppm");
        capture({.18,1.29,.74},{0,1.27,0},"goblin-face.ppm");
        capture({2.2,.91,.7},{0,.81,0},"goblin-side.ppm");
        capture({-.42,.83,2.5},{0,.80,0},"goblin-stride.ppm",Pi*.5,1);
    }
    SDL_GL_DeleteContext(ctx);SDL_DestroyWindow(w);SDL_Quit();
    std::cout<<"Geometry, animated pose and OpenGL rendering passed\n";
}
