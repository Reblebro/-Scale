#include "../src/render.hpp"
#include "../src/platform.hpp"
#include "../src/world.hpp"
#include "../src/arboreal-mesh.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>
// Compare the old reversed-triangle technique with the single two-sided
// draw, using the same OpenGL shader, light, camera and actual leaf geometry.
int main(int argc,char** argv){
    if(SDL_Init(platform::InitVideo))throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(17,3);SDL_GL_SetAttribute(18,3);SDL_GL_SetAttribute(21,1);SDL_GL_SetAttribute(6,24);
    auto* window=SDL_CreateWindow("leaf smoke",0,0,480,320,platform::OpenGL|platform::Hidden);
    if(!window)throw std::runtime_error(SDL_GetError());
    auto context=SDL_GL_CreateContext(window);if(!context)throw std::runtime_error(SDL_GetError());
    {
        Renderer r("assets");Geometry shell,sprigs,old;
        arbor::mesh(shell,sprigs,{{0,0,0},{12,12,12}},[](Cell p){
            return length(p.v()-Vec{6,6,6})<4.5?int(Leaves):int(Air);
        },true);
        assert(sprigs.points.size()>0);
        for(std::size_t i=0;i<sprigs.points.size();i+=3){
            for(int j=0;j<3;++j)old.points.push_back(sprigs.points[i+j]);
            for(int j:{0,2,1}){auto v=sprigs.points[i+j];v.nx=-v.nx;v.ny=-v.ny;v.nz=-v.nz;old.points.push_back(v);}
        }
        Mesh both,single,tree;both.upload(old);single.upload(sprigs);tree.upload(shell);
        both.terrainSurface=single.terrainSurface=tree.terrainSurface=true;single.twoSidedLeaf=true;
        assert(both.vertices==2*single.vertices);
        View v;v.camera={-4,5,-9};v.forward=unit(Vec{6,6,6}-v.camera);v.right=unit(cross(v.forward,{0,1,0}));v.up=cross(v.right,v.forward);
        v.ambient=.85;v.fog=70;v.forestLighting=true;
        for(bool optimized:{false,true}){
            r.start(v,480,320);if(r.beginCanopy(222)){
                assert(r.canopyIntersects({{0,0,0},{12,12,12}}));
                assert(!r.canopyIntersects({{1000,1000,1000},{1032,1064,1032}}));
                r.shadowTree(tree,{});r.endCanopy();
            }
            r.faceCulling(true);r.draw(tree);r.faceCulling(!optimized);r.draw(optimized?single:both);
            r.faceCulling(false);r.finishHud();r.settle();
            if(argc>1)r.screenshot(std::filesystem::path(argv[1])/(optimized?"single.ppm":"double.ppm"));
        }
        std::cout<<"PASS: leaf vertices "<<both.vertices<<" -> "<<single.vertices<<", conservative shadow culling\n";
    }
    SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();
}
