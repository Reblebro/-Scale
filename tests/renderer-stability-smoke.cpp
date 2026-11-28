// Standalone regression: c++ -std=c++20 -O1 -Isrc tests/renderer-stability-smoke.cpp src/render.cpp -Wl,-l:libSDL2-2.0.so.0 -o /tmp/renderer-stability
// xvfb-run -a /tmp/renderer-stability /tmp/renderer-stability-frames
#include "render.hpp"
#include "platform.hpp"
#include "world.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <numeric>
#include <algorithm>
namespace fs=std::filesystem;
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
std::vector<unsigned char> read(const fs::path& p,int w,int h){
    std::ifstream f(p,std::ios::binary);std::string m;int a,b,c;f>>m>>a>>b>>c;f.get();
    require(m=="P6"&&a==w&&b==h&&c==255,"invalid image");
    std::vector<unsigned char> result(w*h*3);require(bool(f.read(reinterpret_cast<char*>(result.data()),result.size())),"short image");return result;
}
double difference(const std::vector<unsigned char>& a,const std::vector<unsigned char>& b){
    double sum=0;for(size_t i=0;i<a.size();++i)sum+=std::abs(int(a[i])-int(b[i]));return sum/a.size();
}
int main(int argc,char** argv){
    const fs::path out=argc>1?argv[1]:fs::temp_directory_path()/"scale-renderer-stability";fs::create_directories(out);
    require(SDL_Init(platform::InitVideo)==0,SDL_GetError());
    SDL_GL_SetAttribute(17,3);SDL_GL_SetAttribute(18,3);SDL_GL_SetAttribute(21,1);SDL_GL_SetAttribute(6,24);
    constexpr int w=800,h=450;
    auto* win=SDL_CreateWindow("shadow stability",0,0,w,h,platform::OpenGL|platform::Hidden);require(win,SDL_GetError());
    auto ctx=SDL_GL_CreateContext(win);require(ctx,SDL_GetError());
    auto glError=reinterpret_cast<unsigned(*)()>(SDL_GL_GetProcAddress("glGetError"));require(glError,"no glGetError");
    {
        Renderer r("assets");
        GraphicsOptions opt;opt.preset(0);require(opt.fps==1 && opt.rays==0,"laptop preset");
        opt.preset(3);require(opt.shadows==5 && opt.rays==4 && opt.shadowFilter==3 && opt.anisotropy==4 && opt.resolution==100,"desktop preset");
        // A very oblique light over a large sloping receiver. No independent
        // caster is present; the plane must not cast a striped shadow on itself.
        Geometry g,block;g.quad({-20,24,-20},{-20,40,20},{20,40,20},{20,24,-20},Stone,{.85f,.85f,.85f});
        block.box({-.7,31.7,1},{.7,35.5,2.4},Stone,{.7f,.7f,.7f});
        Mesh plane,obstacle;plane.upload(g);obstacle.upload(block);
        View v;v.camera={0,36,-8};v.forward=unit(Vec{0,-.48,1});v.right=unit(cross(v.forward,{0,1,0}));v.up=cross(v.right,v.forward);
        v.fog=70;v.far=100;v.near=.025;v.ambient=.85;v.sun=v.shadowSun=unit({.08,.52,.85});v.forestLighting=true;
        r.graphics().preset(3);r.graphics().rays=0;r.graphics().aa=0;r.graphics().hdr=0;r.graphics().bloom=0;r.graphics().occlusion=0;
        auto frame=[&](const char* name,int caster,bool local,bool visibleBlock){
            r.start(v,w,h);if(r.beginCanopy(caster+10)){if(caster==1)r.shadowTree(plane,{});if(caster==2)r.shadowTree(obstacle,{});r.endCanopy();}
            if(local){require(r.beginCreatureShadow({0,33,0},14),"local map failed");r.shadowCreature(plane,{},1,0,0);r.endCreatureShadow();}
            r.draw(plane);if(visibleBlock)r.draw(obstacle);r.finishHud();r.settle();r.screenshot(out/name);require(glError()==0,"GL error");return read(out/name,w,h);
        };
        auto empty=frame("empty.ppm",0,false,false);
        auto self=frame("self.ppm",1,false,false);
        double acne=difference(empty,self);std::cout<<"canopy self-shadow mean error "<<acne<<'\n';require(acne<.65,"sloped plane shadow acne");
        auto local=frame("local-self.ppm",0,true,false);double localAcne=difference(empty,local);std::cout<<"local self-shadow mean error "<<localAcne<<'\n';require(localAcne<.65,"local plane shadow acne");
        auto shadow=frame("caster.ppm",2,false,false);require(difference(empty,shadow)>.08,"real cast shadow missing");
        // Check AO on the same plane with no creases or occluders, including
        // maximum supersampling and the reduced laptop render scale.
        for(int scale:{70,100,150,200}){
            r.graphics().resolution=scale;r.graphics().occlusion=0;
            auto off=frame(("plane-"+std::to_string(scale)+"-off.ppm").c_str(),0,false,false);
            r.graphics().occlusion=4;
            auto on=frame(("plane-"+std::to_string(scale)+"-ao.ppm").c_str(),0,false,false);
            double err=difference(off,on);std::cout<<"AO planar error "<<scale<<"%: "<<err<<'\n';
            require(err<.75,"AO bands on a flat receiver");
        }
        r.graphics().preset(3);r.graphics().resolution=100;frame("desktop-ultra.ppm",2,true,true);
        // High-scale logarithmic depth remains compatible with all effects.
        v.near=.000002;r.graphics().resolution=150;frame("desktop-log-depth.ppm",2,true,true);
    }
    SDL_GL_DeleteContext(ctx);SDL_DestroyWindow(win);SDL_Quit();
    std::cout<<"PASS: desktop/laptop presets, receiver-plane PCF, local map depth, real shadows, AO across four render scales, Ultra and logarithmic depth\n";
}
