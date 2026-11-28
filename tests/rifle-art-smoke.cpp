#include "../src/rifle-art.hpp"
#include "../src/platform.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
static void check(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
int main(int argc,char** argv){
    Geometry body,optic,glow,lens;rifleart::build(body,optic,glow,lens);
    for(auto* mesh:{&body,&optic,&glow}){
        check(mesh->points.size()>100 && mesh->points.size()%3==0,"invalid rifle mesh");
        for(const auto& v:mesh->points){
            check(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z),"invalid vertex");
            check(std::abs(length({v.nx,v.ny,v.nz})-1)<.001,"invalid normal");
            check((v.id>=float(RifleMetalTexture) && v.id<=float(RifleGripTexture)) || (v.id>=float(SuitWeaveTexture) && v.id<=float(SuitLightTexture)),"invalid material");
        }
    }
    std::array<size_t,4> suit{};
    for(const auto& v:body.points)if(v.id>=float(SuitWeaveTexture) && v.id<=float(SuitLightTexture))++suit[size_t(v.id-float(SuitWeaveTexture))];
    for(auto n:suit)check(n>0,"missing suit material");
    check(suit[3]*10<suit[0]+suit[1]+suit[2],"too much emissive suit geometry");
    for(const auto& v:glow.points)check(v.id==float(RifleGlowTexture),"accent must use emissive material");
    // The sight window must be open at the center, with no opaque polygon
    // intersecting its central sightline.
    for(size_t i=0;i<optic.points.size();i+=3){
        auto p=[&](size_t n){auto& v=optic.points[i+n];return Vec{v.x,v.y,v.z};};
        const Vec a=p(0),b=p(1),c=p(2),d={0,0,1};
        const Vec e=b-a,f=c-a,h=cross(d,f);const double det=dot(e,h);
        if(std::abs(det)<1e-10)continue;
        const Vec s=Vec{0,.316,-1}-a;const double u=dot(s,h)/det;
        const auto q=cross(s,e);const double v=dot(d,q)/det,t=dot(f,q)/det;
        check(u<0||v<0||u+v>1||t<0,"opaque geometry blocks sight window");
    }
    check(SDL_Init(platform::InitVideo)==0,SDL_GetError());
    SDL_GL_SetAttribute(17,3);SDL_GL_SetAttribute(18,3);SDL_GL_SetAttribute(21,1);SDL_GL_SetAttribute(6,24);
    auto* w=SDL_CreateWindow("rifle art",0,0,960,540,platform::OpenGL|platform::Hidden);check(w,SDL_GetError());
    auto context=SDL_GL_CreateContext(w);check(context,SDL_GetError());
    auto error=reinterpret_cast<unsigned(*)()>(SDL_GL_GetProcAddress("glGetError"));
    const std::filesystem::path out=argc>1?argv[1]:"/tmp/scale-rifle-art";
    std::filesystem::create_directories(out);
    {
        Renderer r("assets");Mesh b,o,g,l;b.upload(body);o.upload(optic);g.upload(glow);l.upload(lens);
        View view;view.forward={0,0,1};view.right={-1,0,0};view.up={0,1,0};view.ambient=.72f;view.fog=80;
        const auto render=[&](const char* name,Vec at,double yaw){
            r.start(view,960,540);r.drawSky();
            r.draw(b,at,rifleart::Scale,yaw,nullptr,.68f);r.draw(o,at,rifleart::Scale,yaw,nullptr,.68f);
            r.translucent(true);r.draw(l,at,rifleart::Scale,yaw);r.translucent(false);
            r.draw(g,at,rifleart::Scale,yaw,nullptr,.4f);
            r.finishHud();r.settle();r.screenshot(out/name);check(error()==0,"OpenGL rifle render error");
        };
        render("hip.ppm",{-rifleart::Side,-rifleart::Drop,rifleart::Forward},rifleart::HipTurn);
        render("aim.ppm",{0,-rifleart::AimDrop,rifleart::Forward},0);
        r.graphics().aa=3;r.graphics().bloom=2;r.graphics().hdr=1;
        render("high.ppm",{-rifleart::Side,-rifleart::Drop,rifleart::Forward},rifleart::HipTurn);
        // Inspect the actual supporting glove independently of weapon occlusion.
        Geometry suitGeometry;techsuit::build(suitGeometry);Mesh glove;glove.upload(suitGeometry);
        view.camera={1.24,.18,-.20};view.forward=unit(Vec{.22,-.30,.04}-view.camera);
        view.right=unit(cross(view.forward,{0,1,0}));view.up=cross(view.right,view.forward);
        view.fov=2.8f;r.graphics().preset(1);r.graphics().aa=3;
        r.start(view,960,540);r.drawSky();r.draw(glove,{},1,0,nullptr,.68f);
        r.finishHud();r.settle();r.screenshot(out/"suit-detail.ppm");check(error()==0,"OpenGL suit detail error");
    }
    SDL_GL_DeleteContext(context);SDL_DestroyWindow(w);SDL_Quit();
    std::cout<<"PASS: rifle geometry, material layers, open optic and OpenGL previews: "<<body.points.size()/3<<" body triangles\n";
}
