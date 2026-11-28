#include "render.hpp"
#include "world.hpp"
#include "daylight.hpp"
#include "platform.hpp"
#include <iostream>
#include <stdexcept>
// Run from the game root with an OpenGL 3.3 context, e.g.:
// c++ -std=c++20 -O1 -Isrc tests/canopy-light-smoke.cpp src/render.cpp -Wl,-l:libSDL2-2.0.so.0 -o /tmp/canopy-smoke
// xvfb-run -a /tmp/canopy-smoke
int main(int argc,char** argv){
    const std::filesystem::path out=argc>1?argv[1]:"";
    if(!out.empty())std::filesystem::create_directories(out);
    if(SDL_Init(platform::InitVideo))throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(17,3);SDL_GL_SetAttribute(18,3);SDL_GL_SetAttribute(21,1);SDL_GL_SetAttribute(6,24);
    auto* window=SDL_CreateWindow("lighting smoke",0,0,640,360,platform::OpenGL|platform::Hidden);
    if(!window)throw std::runtime_error(SDL_GetError());
    auto context=SDL_GL_CreateContext(window);if(!context)throw std::runtime_error(SDL_GetError());
    {
        Renderer r("assets");
        Geometry g,t;g.box({-35,28,-35},{35,29,35},Grass);
        // Several tall trunks and canopy masses, leaving deliberate openings.
        for(int x:{-15,-5,5,15})for(int z:{-15,-3,9,21}){
            t.box({double(x)-1,29,double(z)-1},{double(x)+1,40,double(z)+1},Timber);
            if(x!=-5 || z!=9)t.box({double(x)-4,39,double(z)-4},{double(x)+4,43,double(z)+4},Leaves);
        }
        Mesh ground,trees;ground.uploadTerrain(g);trees.uploadTerrain(t);
        View v;v.camera={-1,31,-21};v.forward=unit({.08,.08,1});v.right=unit(cross(v.forward,{0,1,0}));v.up=cross(v.right,v.forward);v.ambient=.95;v.skyMode=1;v.fog=85;v.forestLighting=true;
        for(bool enabled:{false,true}){
            v.forestLighting=enabled;r.start(v,640,360);
            if(enabled){if(!r.beginCanopy(123))throw std::runtime_error("shadow map not available");r.shadowTree(trees,{});r.endCanopy();}
            r.draw(ground);r.draw(trees);r.drawSky();r.finishHud();r.settle();
            if(!out.empty())r.screenshot(out/(enabled?"light-on.ppm":"light-off.ppm"));
        }
        r.start(v,640,360);if(r.beginCanopy(123))throw std::runtime_error("cache unexpectedly rebuilt");
        r.draw(ground);r.draw(trees);r.drawSky();r.finishHud();r.settle();if(!out.empty())r.screenshot(out/"light-cached.ppm");
        r.start(v,640,360);if(!r.beginCanopy(124))throw std::runtime_error("cache did not invalidate");r.shadowTree(trees,{});r.endCanopy();r.draw(ground);r.draw(trees);r.drawSky();r.finishHud();r.settle();
        r.renderScale(60);r.start(v,640,360);if(r.beginCanopy(124))throw std::runtime_error("cache unexpectedly rebuilt at scaled resolution");r.draw(ground);r.draw(trees);r.drawSky();r.finishHud();r.settle();if(!out.empty())r.screenshot(out/"light-scaled.ppm");
        v.dayCycle=true;v.sun=solarcycle::at(450).sun;v.shadowSun=solarcycle::shadowSun(450);v.daylight=1;
        r.start(v,640,360);if(!r.beginCanopy(124))throw std::runtime_error("moving sun did not invalidate shadow cache");
        r.shadowTree(trees,{});r.endCanopy();r.draw(ground);r.draw(trees);r.drawSky();r.finishHud();r.settle();
        r.start(v,640,360);if(r.beginCanopy(124))throw std::runtime_error("solar cache not reused");
        r.draw(ground);r.draw(trees);r.drawSky();r.finishHud();r.settle();
        v.sun=solarcycle::at(0).sun;v.shadowSun=v.sun;v.daylight=0;v.ambient=.115;v.forestLighting=false;
        v.sky={.021f,.038f,.065f};v.shadowWeight=0;
        std::array<Light,8> lights{};lights[0]={{0,31,-12},3,true};lights[1]={{0,31,-3},3,true};
        r.start(v,640,360,lights);if(r.beginCanopy(124))throw std::runtime_error("night shadow pass should be disabled");
        r.draw(ground);r.draw(trees);r.drawSky();r.finishHud();r.settle();
        if(!out.empty())r.screenshot(out/"cycle-night.ppm");
    }
    SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();std::cout<<"PASS: OpenGL shader compilation, shadow FBO, cached map, moving sun, lanterns, nighttime and scaled scene\n";
}
