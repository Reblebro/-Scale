#include "../src/render.hpp"
#include "../src/platform.hpp"
#include "../src/world.hpp"
#include "../src/natural-world.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <numeric>
static void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(int argc,char** argv){
    namespace fs=std::filesystem;
    const fs::path out=argc>1?argv[1]:fs::temp_directory_path()/"scale-graphics-options-smoke";
    fs::create_directories(out);
    GraphicsOptions settings;
    settings.preset(3);settings.adjust(0,1);settings.fps=3;settings.save(out/"graphics.txt");
    GraphicsOptions restored;restored.load(out/"graphics.txt");
    check(settings==restored,"graphics settings did not persist");
    {std::ofstream f(out/"bad.txt");f<<"SCALE_GRAPHICS 1\n999 2 99 4 9 8 7 6 4 12 42 42\n";}
    restored.load(out/"bad.txt");check(restored.resolution==200 && restored.occlusion==4 && restored.fps==4,"invalid settings not clamped");
    fs::remove(out/"graphics.txt");fs::remove(out/"bad.txt");
    check(SDL_Init(platform::InitVideo)==0,SDL_GetError());
    SDL_GL_SetAttribute(17,3);SDL_GL_SetAttribute(18,3);SDL_GL_SetAttribute(21,1);SDL_GL_SetAttribute(6,24);
    auto* window=SDL_CreateWindow("graphics smoke",0,0,640,360,platform::OpenGL|platform::Hidden);check(window,SDL_GetError());
    auto ctx=SDL_GL_CreateContext(window);check(ctx,SDL_GetError());
    auto error=reinterpret_cast<unsigned(*)()>(SDL_GL_GetProcAddress("glGetError"));check(error,"glGetError missing");
    {
        Renderer r("assets");
        Geometry earth,trees,posts,flames;
        constexpr std::uint64_t seed=62108432;
        // Real continuous edge field on a sloping roof; same signed distance
        // is assigned to shared corners. No overlay mesh above the ground.
        const int lane=0;const double center=natural::trailX(lane,40,seed);
        const int cx=int(std::floor(center));
        for(int z=28;z<64;z++)for(int x=cx-12;x<cx+13;x++){
            auto h=[&](int xx,int zz){return natural::surfaceHeight(xx,zz,seed);};
            auto point=[&](int xx,int zz){return Vec{double(xx),h(xx,zz),double(zz)};};
            const auto begin=earth.points.size();
            earth.quad(point(x,z),point(x,z+1),point(x+1,z+1),point(x+1,z),TrailSurfaceTexture,{1,1,1});
            for(std::size_t i=begin;i<earth.points.size();++i){auto& v=earth.points[i];v.u=float(natural::trailEdge(v.x,v.z,seed));}
        }
        for(int z:{34,42,50,58}){
            double x=natural::trailX(lane,z,seed)+(z%16==2?-2.5:2.5);
            const Vec p{x,natural::surfaceHeight(x,z,seed),double(z)};
            posts.tube(p,p+Vec{0,1.7,0},.10,Timber,{.8f,.7f,.6f});
            flames.box(p+Vec{-.18,1.7,-.18},p+Vec{.18,2.1,.18},Lamp,{1,.7f,.3f});
            for(int side:{-1,1}){
                const Vec t{natural::trailX(lane,z,seed)+side*5.5,natural::surfaceHeight(natural::trailX(lane,z,seed)+side*5.5,z,seed),double(z)};
                trees.tube(t,t+Vec{0,8,0},.8,Timber,{1,1,1},12);
                trees.stone(t+Vec{0,8,0},3.4,Leaves,{1,1,1},6,12);
            }
        }
        Mesh g,t,p,f;g.uploadTerrain(earth);t.uploadTerrain(trees);p.upload(posts);f.upload(flames);
        View v;v.camera={natural::trailX(lane,31,seed),natural::surfaceHeight(natural::trailX(lane,31,seed),31,seed)+1.7,31};
        v.forward=unit(Vec{natural::trailX(lane,48,seed)-v.camera.x,-.12,17});v.right=unit(cross(v.forward,{0,1,0}));v.up=cross(v.right,v.forward);
        v.forestLighting=true;v.skyMode=1;v.ambient=.70f;v.fog=70;
        auto render=[&](const char* name,std::uint64_t revision){
            r.start(v,640,360);if(r.beginCanopy(revision)){r.shadowTree(t,{});r.endCanopy();}
            r.draw(g);r.draw(t);r.draw(p);r.draw(f,{},1,0,nullptr,2.4f);r.drawSky();
            // Draw the HUD after the effects; the exact color must survive.
            r.rect(5,5,14,14,{1,0,1});r.finishHud();r.settle();r.screenshot(out/name);
            check(error()==0,"OpenGL error in a graphics mode");
        };
        auto pixels=[&](const char* name){
            std::ifstream f(out/name,std::ios::binary);std::string s;int w,h,max;f>>s>>w>>h>>max;f.get();
            check(w==640 && h==360 && max==255,"invalid output size");
            std::vector<unsigned char> p((std::istreambuf_iterator<char>(f)),{});
            check(p.size()==640*360*3,"invalid output pixels");
            const auto i=(10*640+10)*3;check(p[i]>250 && p[i+1]<5 && p[i+2]>250,"HUD changed by post effect");
            return p;
        };
        render("default.ppm",1);auto base=pixels("default.ppm");
        r.graphics().aa=1;render("fxaa.ppm",1);auto aa=pixels("fxaa.ppm");check(aa!=base,"FXAA no effect");
        // Request actual multisampling, not a renamed post filter. Drivers may cap it.
        r.graphics().aa=3;render("msaa4.ppm",1);check(r.activeSamples()>=2,"no actual multisample target");
        check(pixels("msaa4.ppm")!=base,"MSAA no effect");
        r.graphics().aa=4;render("msaa8.ppm",1);check(r.activeSamples()>=2 && r.activeSamples()<=8,"invalid hardware sample count");pixels("msaa8.ppm");
        r.graphics().aa=5;render("hybrid.ppm",1);pixels("hybrid.ppm");
        r.graphics().aa=0;r.graphics().hdr=1;render("filmic.ppm",1);check(pixels("filmic.ppm")!=base,"tone mapping no effect");
        r.graphics().exposure=4;render("exposure.ppm",1);check(pixels("exposure.ppm")!=pixels("filmic.ppm"),"exposure no effect");
        r.graphics().hdr=0;r.graphics().exposure=2;r.graphics().aa=0;r.graphics().bloom=3;render("bloom.ppm",1);auto bloom=pixels("bloom.ppm");
        check(std::accumulate(bloom.begin(),bloom.end(),0LL)>std::accumulate(base.begin(),base.end(),0LL),"bloom no effect");
        r.graphics().bloom=0;r.graphics().occlusion=3;render("ao.ppm",1);auto ao=pixels("ao.ppm");
        check(std::accumulate(ao.begin(),ao.end(),0LL)<std::accumulate(base.begin(),base.end(),0LL),"AO no effect");
        r.graphics().preset(3);render("ultra.ppm",1);pixels("ultra.ppm");
        r.graphics().preset(0);render("low.ppm",1);pixels("low.ppm");
        r.graphics().resolution=25;render("quarter.ppm",1);pixels("quarter.ppm");
        r.graphics().preset(1);render("restored.ppm",1);check(pixels("restored.ppm")==base,"default did not restore baseline");
        r.graphics().preset(3);v.near=.000002;v.far=90;render("log-depth.ppm",2);pixels("log-depth.ppm");
        r.graphics().shadows=0;r.graphics().rays=0;render("shadows-off.ppm",2);pixels("shadows-off.ppm");
    }
    SDL_GL_DeleteContext(ctx);SDL_DestroyWindow(window);SDL_Quit();
    std::cout<<"PASS: persisted/clamped settings, FXAA, bloom, depth-aware AO, native/low/supersampled resolution, shadow quality changes, log depth, identical restored default and unfiltered HUD\n";
}
