#include "../src/north-art.hpp"
#include "../src/micro.hpp"
#include "../src/platform.hpp"
#include <cassert>
#include <iostream>
#include <cstring>
#include <chrono>
int main(int argc,char** argv){
    northart::Rig rig,low;rig.build();low.build(true);
    Geometry a,b,c,portal,ground;
    rig.pose(a,0);rig.pose(b,4.89);low.pose(c,0);micro::gate(portal);
    assert(rig.joints.size()>100 && a.points.size()==b.points.size() && a.points.size()>c.points.size());
    Geometry slow,fast;
    for(int i=0;i<12;i++){
        const double t=i*.43, stride=i*.71;
        rig.pose(slow,t,stride,.8,false);rig.pose(fast,t,stride,.8,true);
        assert(slow.points.size()==fast.points.size());
        assert(std::memcmp(slow.points.data(),fast.points.data(),fast.points.size()*sizeof(Vertex))==0);
    }
    size_t unique=0;for(const auto& joint:rig.joints)unique+=joint.unique.size();
    Geometry compact;
    rig.pose(compact,0,0,0,true,true);
    assert(compact.points.size()==unique && rig.indices.size()==a.points.size());
    for(size_t i=0;i<a.points.size();i++)assert(std::memcmp(&a.points[i],&compact.points[rig.indices[i]],sizeof(Vertex))==0);
    auto timed=[&](bool indexed){
        const auto start=std::chrono::steady_clock::now();
        for(int i=0;i<80;i++)rig.pose(fast,i*.13,i*.24,.8,indexed,indexed);
        return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    };
    std::cout<<unique<<" unique of "<<a.points.size()<<" vertex transforms; direct "<<timed(false)<<" ms, compact indexed "<<timed(true)<<" ms for 80 poses\n";
    size_t moving=0,energy=0; double base=1,top=0;
    for(size_t i=0;i<a.points.size();i++){
        auto v=b.points[i],w=a.points[i];
        assert(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z));
        assert(std::abs(length({v.nx,v.ny,v.nz})-1)<.002);
        moving+=length({v.x-w.x,v.y-w.y,v.z-w.z})>.001;
    }
    for(size_t i=0;i<rig.joints.size();i++)assert(rig.joints[i].parent<int(i));
    for(auto v:portal.points){base=std::min(base,double(v.y));top=std::max(top,double(v.y));if(int(v.id)==GateEnergyTexture){++energy;assert(std::hypot(v.u,v.v)<1.001);}}
    assert(moving>a.points.size()/5 && energy==64*3 && base<-.3 && top>1.7);
    std::cout<<rig.joints.size()<<" joints, "<<a.points.size()/3<<" triangles, "<<low.joints.size()<<" distant joints, "<<c.points.size()/3<<" distant triangles; gate "<<portal.points.size()/3<<" triangles\n";
    if(SDL_Init(platform::InitVideo))throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(17,3);SDL_GL_SetAttribute(18,3);SDL_GL_SetAttribute(21,1);SDL_GL_SetAttribute(6,24);
    auto* w=SDL_CreateWindow("North and gate",0,0,1280,720,platform::OpenGL|platform::Hidden);
    if(!w)throw std::runtime_error(SDL_GetError());auto ctx=SDL_GL_CreateContext(w);if(!ctx)throw std::runtime_error(SDL_GetError());
    {
        Renderer r("assets"); Mesh m,p,g; m.upload(a);p.upload(portal);
        ground.box({-14,-.3,-14},{14,0,14},Grass);g.uploadTerrain(ground);
        auto shot=[&](const char* label,double time,bool shadow,bool dark,Vec camera,Vec target){
            View v;v.camera=camera;v.forward=unit(target-camera);v.right=unit(cross(v.forward,{0,1,0}));v.up=cross(v.right,v.forward);
            v.fog=40;v.far=60;v.ambient=dark?.14f:.70f;v.sky={.085f,.10f,.12f};v.effectTime=time;
            v.forestLighting=true;v.sun=v.shadowSun=unit({-.55,.7,.48});v.gateLightCount=1;v.gateLights[0]={2,1,0};
            r.start(v,1280,720);
            if(r.beginCanopy(1))r.endCanopy();
            if(shadow){assert(r.beginCreatureShadow({0,1,0},4));r.shadowCreature(m,{},1,0,0);r.endCreatureShadow();}
            r.draw(g);r.faceCulling(false);r.draw(m);r.draw(p,{2,0,0});r.finishHud();r.settle();
            if(argc>1)r.screenshot(std::filesystem::path(argv[1])/label);
        };
        shot("north-day.ppm",0,false,false,{.6,1.35,3.8},{.75,1.05,0});
        shot("north-shadow.ppm",0,true,false,{.6,1.35,3.8},{.75,1.05,0});
        m.uploadIndexed(compact,rig.indices);
        shot("north-indexed.ppm",0,true,false,{.6,1.35,3.8},{.75,1.05,0});
        m.upload(b);shot("north-night.ppm",4.89,true,true,{.6,1.35,3.8},{.75,1.05,0});
        shot("north-face.ppm",4.89,true,false,{.06,1.9,1.05},{0,1.94,0});
        shot("gate-close.ppm",5.6,false,true,{2.3,1.3,2.9},{2,1,0});
    }
    SDL_GL_DeleteContext(ctx);SDL_DestroyWindow(w);SDL_Quit();
    std::cout<<"North rig, portal mesh and shadow rendering passed\n";
}
