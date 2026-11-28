// c++ -std=c++20 -O1 -Isrc tests/scale-detail-smoke.cpp src/render.cpp src/world-picker.cpp -Wl,-l:libSDL2-2.0.so.0 -o /tmp/scale-detail-smoke
// xvfb-run -a /tmp/scale-detail-smoke /tmp/scale-detail-preview
#include "render.hpp"
#include "world.hpp"
#include "world-picker.hpp"
#include "platform.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace fs=std::filesystem;
static std::vector<unsigned char> readImage(const fs::path& path){
    std::ifstream input(path,std::ios::binary);std::string magic;int w,h,max;
    input>>magic>>w>>h>>max;input.get();
    if(magic!="P6" || w!=640 || h!=360 || max!=255)throw std::runtime_error("missing frame");
    std::vector<unsigned char> bytes(w*h*3);
    if(!input.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("incomplete frame");
    return bytes;
}
int main(int argc,char** argv){
    if(argc!=2)throw std::runtime_error("pass an output directory");
    const fs::path out=argv[1];fs::create_directories(out);
    if(SDL_Init(platform::InitVideo))throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(17,3);SDL_GL_SetAttribute(18,3);SDL_GL_SetAttribute(21,1);SDL_GL_SetAttribute(6,24);
    auto* window=SDL_CreateWindow("scale detail smoke",0,0,1280,720,platform::OpenGL|platform::Hidden);
    if(!window)throw std::runtime_error(SDL_GetError());
    auto context=SDL_GL_CreateContext(window);if(!context)throw std::runtime_error(SDL_GetError());
    {
        Renderer renderer("assets");
        for(int tier:{1,4,8}){
            const double size=1.0/subdivisions(tier);
            // Large world coordinates plus small local vertices test the
            // camera-relative phase rather than world-position float rounding.
            const Vec origin{1500,29,-1300};
            Geometry g;g.box({-5*size,-size,-5*size},{5*size,0,5*size},Dirt);
            Mesh ground;ground.uploadTerrain(g);
            View v;v.camera=origin+Vec{.17*size,1.4*size,-.13*size};
            v.forward={0,-1,0};v.right={1,0,0};v.up={0,0,1};
            v.near=.02*size;v.far=150;v.fog=100;v.ambient=.95;
            const auto render=[&](double body,const char* name){
                v.detailBody=body;renderer.start(v,640,360);renderer.draw(ground,origin);renderer.finishHud();renderer.settle();
                const auto path=out/(std::to_string(tier)+name+".ppm");renderer.screenshot(path);return readImage(path);
            };
            const auto old=render(1,"-base"),now=render(size,"-detail");
            double changed=0;for(std::size_t i=0;i<old.size();++i)changed+=std::abs(int(old[i])-int(now[i]));changed/=old.size();
            if(changed<.25)throw std::runtime_error("microstructure did not affect the visible surface");
            const auto left=render(size*1.00001,"-above"),right=render(size*.99999,"-below");
            double seam=0;for(std::size_t i=0;i<left.size();++i)seam+=std::abs(int(left[i])-int(right[i]));seam/=left.size();
            if(seam>1.5)throw std::runtime_error("detail jumps at the octave boundary");
            std::cout<<"tier "<<tier<<": mean detail change "<<changed<<", octave seam "<<seam<<"\n";
        }
        worldPickerPlayTests(renderer);
    }
    SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();
    std::cout<<"PASS: microscopic shader detail, octave continuity, confirmed world picker deletion\n";
}
