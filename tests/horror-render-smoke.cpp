#include "../src/atmosphere.hpp"
#include "../src/platform.hpp"
#include "../src/world.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>
// Standalone real OpenGL portrait and rig test (no large world archive needed).
int main(int argc,char** argv){
    Geometry head,left,right,body;
    witnessHead(head);witnessEye(left,true);witnessEye(right,false);
    assert(head.points.size()>110000 && head.points.size()<160000);
    for(const auto& p:head.points){
        assert(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z));
        assert(std::abs(length({p.nx,p.ny,p.nz})-1)<.001);
        assert(std::abs(p.y)<=.406 && std::abs(p.x)<.33 && std::abs(p.z)<.38);
    }
    std::size_t bone=0,teeth=0;
    double brightest=0,darkest=1;
    for(const auto& v:head.points){
        if(int(v.id)==WitnessBoneTexture){++bone;brightest=std::max(brightest,double(v.r));darkest=std::min(darkest,double(v.r));}
        else if(int(v.id)==WitnessToothTexture)++teeth;
        else assert(int(v.id)==WitnessSkinTexture && "unexpected skull material");
    }
    assert(bone>110000 && teeth>500 && brightest>.60 && darkest<.02);
    assert(witnessSocket(-1).y>witnessSocket(1).y);
    std::cout<<"Eye centers "<<witnessSocket(-1).z<<", "<<witnessSocket(1).z<<"\n";
    // Probe the actual triangles, not a texture or a precomputed mask.
    // The eye and mouth must recess physically relative to the brow and chin.
    const auto faceDepth=[&](double x,double y){
        const Vec from{x,y,.6},dir{0,0,-1};double depth=-1;
        for(std::size_t i=0;i<head.points.size();i+=3){
            const auto point=[](const Vertex& v){return Vec{v.x,v.y,v.z};};
            const Vec a=point(head.points[i]),b=point(head.points[i+1]),c=point(head.points[i+2]);
            const Vec e=b-a,f=c-a,h=cross(dir,f);const double det=dot(e,h);
            if(std::abs(det)<1e-12)continue;
            const Vec q=from-a;const double u=dot(q,h)/det;
            const Vec r=cross(q,e);const double v=dot(dir,r)/det,t=dot(f,r)/det;
            if(u>=0 && v>=0 && u+v<=1 && t>=0)depth=std::max(depth,from.z-t);
        }
        return depth;
    };
    const double leftDepth=faceDepth(witnessSocket(-1).x,witnessSocket(-1).y);
    const double rightDepth=faceDepth(witnessSocket(1).x,witnessSocket(1).y);
    const double brow=faceDepth(-.10,.204),mouth=faceDepth(-.013,-.20207),chin=faceDepth(-.013,-.28646);
    std::cout<<"Head geometry: "<<head.points.size()/3<<" triangles; eye depths "<<leftDepth<<", "<<rightDepth
        <<"; brow "<<brow<<"; mouth "<<mouth<<"; chin "<<chin<<"\n";
    // Measure the actual eye mesh as well as the recess: a shallow orbit
    // must not be obtained by moving the whole globe outside it.
    assert(brow-leftDepth>.040 && brow-leftDepth<.085);
    assert(brow-rightDepth>.040 && brow-rightDepth<.085);
    for(int side:{-1,1}){
        const Vec c=witnessSocket(side);
        const Geometry& eye=side<0?left:right;
        double r=0,front=-1;
        for(const auto& v:eye.points){
            r=std::max(r,length({v.x,v.y,v.z}));
            front=std::max(front,double(v.z));
        }
        const double floor=faceDepth(c.x,c.y),cap=c.z+front;
        const double upper=faceDepth(c.x,c.y+.051),lower=faceDepth(c.x,c.y-.051);
        const double inner=faceDepth(c.x-side*.064,c.y),outer=faceDepth(c.x+side*.064,c.y);
        assert(c.z-r<floor-.015 && cap>floor+.035);
        assert(c.z-floor>.004 && c.z-floor<.020);
        assert(upper>cap+.020 && lower>cap+.020 && inner>cap+.020);
        assert(outer>c.z+.008);
        std::cout<<"Eye "<<side<<": floor "<<floor<<", front cap "<<cap
            <<", upper rim "<<upper<<", outer rim "<<outer<<"\n";
    }
    assert(mouth+.045<chin);
    const double nasalBridge=faceDepth(.008,-.0466),nasalLeft=faceDepth(-.006,-.0466),nasalRight=faceDepth(.022,-.0466);
    std::cout<<"Nasal septum "<<nasalBridge<<"; apertures "<<nasalLeft<<", "<<nasalRight<<"\n";
    assert(nasalBridge>nasalLeft+.055 && nasalBridge>nasalRight+.055);

    contact::Queries ground;
    ground.cast=[](Vec from,Vec d,double reach)->std::optional<contact::Surface>{
        if(d.y>=-1e-9 || from.y<=0)return {};
        double t=-from.y/d.y;if(t>reach)return {};
        return contact::Surface{from+d*t,{0,1,0},{}};
    };
    ground.follow=[](contact::Surface&){return true;};
    contact::Rig rig;const contact::Frame frame{{},1,0};
    for(int i=0;i<120;++i)rig.update(frame,true,false,false,{},5.1,1.0/60,ground);
    WitnessSkin skin;skin.build(false);skin.pose(body,rig.pose);
    for(const auto& p:body.points){
        assert(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z));
        if(int(p.id)==WitnessMantleTexture)assert(std::abs(length({p.nx,p.ny,p.nz})-1)<.002);
    }
    assert(std::count_if(body.points.begin(),body.points.end(),[](const Vertex& p){return int(p.id)==WitnessMantleTexture;})>1000);
    if(SDL_Init(platform::InitVideo))throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(17,3);SDL_GL_SetAttribute(18,3);SDL_GL_SetAttribute(21,1);SDL_GL_SetAttribute(6,24);
    auto* w=SDL_CreateWindow("horror test",0,0,960,540,platform::OpenGL|platform::Hidden);
    if(!w)throw std::runtime_error(SDL_GetError());
    auto ctx=SDL_GL_CreateContext(w);if(!ctx)throw std::runtime_error(SDL_GetError());
    {
        Renderer r("assets");r.setNorthTexture();
        Mesh h,l,e,b,g;h.upload(head);l.upload(left);e.upload(right);b.upload(body);
        Geometry terrain;terrain.box({-12,-.15,-12},{12,0,12},Grass);g.uploadTerrain(terrain);
        const auto capture=[&](Vec camera,Vec target,float ambient,const char* name,bool torch=true,float fov=1.0f,bool cover=false){
            View v;v.camera=camera;v.forward=unit(target-camera);v.right=unit(cross(v.forward,{0,1,0}));v.up=cross(v.right,v.forward);
            v.ambient=ambient;v.fov=fov;v.sky={.10f,.15f,.17f};v.fog=50;v.far=60;v.skyMode=0;v.torchRange=torch?8:0;
            std::array<Light,8> lights{};
            if(!torch)lights[0]={rig.pose.neck+Vec{-.9,.35,.95},1.5f,true};
            r.start(v,960,540,lights);r.draw(g);r.draw(b);r.draw(h,rig.pose.neck);
            if(cover){
                Geometry trunk;const Vec center=rig.pose.neck+Vec{-.62,-rig.pose.neck.y,.58};
                trunk.tube(center,center+Vec{0,5.5,0},.60,Timber,{1,1,1},28);
                Mesh tree;tree.uploadTerrain(trunk);r.draw(tree);
            }
            r.draw(l,rig.pose.neck+witnessSocket(-1),1,0,nullptr,.12f);r.draw(e,rig.pose.neck+witnessSocket(1),1,0,nullptr,.12f);
            r.finishHud();r.settle();if(argc>1)r.screenshot(std::filesystem::path(argv[1])/name);
        };
        capture({.05,1.75,1.98},rig.pose.neck,.35f,"witness-face.ppm");
        capture({.70,1.50,4.50},{0,1.15,.25},.38f,"witness-body.ppm",true,2.2f);
        capture(rig.pose.neck+Vec{1.4,.15,.38},rig.pose.neck,.38f,"witness-profile.ppm",true,2.6f);
        capture({4.5,1.9,1.6},{0,.8,0},.40f,"witness-side-body.ppm",true,2.2f);
        capture({1.0,1.9,-4.6},{0,.8,0},.40f,"witness-rear.ppm",true,2.2f);
        capture(rig.pose.neck+Vec{.60,.10,1.30},rig.pose.neck,.09f,"witness-lantern.ppm",false);
        capture(rig.pose.neck+Vec{.04,.02,1.05},rig.pose.neck,.29f,"witness-detail.ppm",true,2.0f);
        capture(rig.pose.neck+Vec{.15,.07,2.65},rig.pose.neck,.065f,"witness-cover.ppm",false,1.6f,true);
        for(int i=0;i<180;++i)rig.update(frame,false,false,false,{},5.1,1.0/60,ground);
        skin.pose(body,rig.pose);b.upload(body);
        capture({1.3,2.4,5.8},{0,rig.pose.neck.y*.54,0},.40f,"witness-standing.ppm",true,1.8f);
    }
    SDL_GL_DeleteContext(ctx);SDL_DestroyWindow(w);SDL_Quit();
    std::cout<<"PASS: sculpted face bounds/normals, recessed eyes, contact rig, OpenGL portrait\n";
}
