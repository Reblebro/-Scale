#include "../src/render.hpp"
#include "../src/platform.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
int main(int argc,char** argv){
    const std::filesystem::path dir=argc>1?argv[1]:"/tmp/scale-planet-smoke";
    std::filesystem::create_directories(dir);
    if(SDL_Init(platform::InitVideo))throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(17,3);SDL_GL_SetAttribute(18,3);SDL_GL_SetAttribute(21,1);SDL_GL_SetAttribute(6,24);
    auto* window=SDL_CreateWindow("fractured world",0,0,960,540,platform::OpenGL|platform::Hidden);
    if(!window)throw std::runtime_error(SDL_GetError());
    auto context=SDL_GL_CreateContext(window);if(!context)throw std::runtime_error(SDL_GetError());
    {
        Renderer r("assets");
        View v;v.forward=unit({.4833,.4214,-.7674});v.right=unit(cross(v.forward,{0,1,0}));v.up=cross(v.right,v.forward);
        v.fov=1.3;v.skyMode=1;v.dayCycle=true;v.fog=120;
        for(bool day:{false,true}){
            v.daylight=day?1:0;v.sun=unit(day?Vec{-.48,.65,.35}:Vec{-.48,-.65,.35});
            v.sky=day?Color{.43f,.61f,.72f}:Color{.018f,.028f,.060f};
            r.start(v,960,540);r.drawSky();r.finishHud();r.settle();
            const auto file=dir/(day?"planet-day.ppm":"planet-night.ppm");r.screenshot(file);
            std::ifstream f(file,std::ios::binary);std::string tag;int w,h,m;f>>tag>>w>>h>>m;f.get();
            assert(tag=="P6" && w>0 && h>0 && m==255);
            std::vector<unsigned char> pixels((std::istreambuf_iterator<char>(f)),{});
            assert(pixels.size()==std::size_t(w*h*3));
            int hot=0;
            for(std::size_t i=0;i<pixels.size();i+=3)
                if(pixels[i]>140 && pixels[i]>pixels[i+1]*1.25 && pixels[i]>pixels[i+2]*1.6)++hot;
            assert(hot>100);std::cout<<(day?"Day":"Night")<<": "<<hot<<" warm fissure pixels\n";
        }
    }
    SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();
    std::cout<<"PASS: actual sky shader at day and night\n";
}
