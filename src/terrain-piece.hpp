#pragma once

#include "math.hpp"
#include <optional>
#include <memory>

struct TerrainFacetHit {double distance;Cell normal;Vec surfaceNormal;};

struct TerrainStone {
    std::array<Vec,8> base,waist,shoulder;
    Vec foot,crown;
    bool grounded{};
    // A joined bank shares its edge samples with the adjoining bank. Keep the
    // closed hull for queries, but submit only sides exposed to the excavation.
    struct Bank {
        std::array<Vec,27> nodes{};
        unsigned visible{};
        Vec& at(Cell p){return nodes[p.x+3*(p.y+3*p.z)];}
        const Vec& at(Cell p) const{return nodes[p.x+3*(p.y+3*p.z)];}
    };
    std::shared_ptr<const Bank> bank;

    explicit TerrainStone(std::shared_ptr<const Bank> shape):bank(std::move(shape)){}

    TerrainStone(std::uint32_t key,bool crystal,bool onFloor):grounded(onFloor){
        // Three distinct, irregular rings produce a broken boulder rather than
        // an eight-sided cuboid topped by a pointy pyramid. The tiny upper cap
        // has almost no rise: no central cone is visible.
        constexpr double tau=6.283185307179586;
        constexpr std::array<Vec,8> foundation={{{0,0,0},{0.5,0,0},{1,0,0},{1,0,0.5},
            {1,0,1},{0.5,0,1},{0,0,1},{0,0,0.5}}};
        const auto scramble=[](std::uint32_t v){
            v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;return v^(v>>16);
        };
        const Vec pivot{0.47+double(key&7)*0.009,0,0.47+double((key>>3)&7)*0.009};
        const double rotation=onFloor?-3.0*tau/8.0:double((key>>9)&255)/255.0*tau;
        for(int i=0;i<8;i++){
            const double angle=rotation+tau*(i+0.08*double(int(scramble(key+i*13)&31)-15)/31.0)/8.0;
            const auto h=scramble(key+std::uint32_t(i)*0x9e3779b9u);
            const double radial=0.34+0.10*double(h&255)/255.0;
            const Vec direction{std::cos(angle),0,std::sin(angle)};
            const double floorY=crystal?0.05:0.0;
            base[i]=onFloor?foundation[i]:pivot+direction*(0.75*radial)+Vec{0,floorY,0};
            waist[i]=pivot+direction*(1.05*radial)+Vec{0,0.34+0.18*double((h>>8)&255)/255.0,0};
            shoulder[i]=pivot+direction*(0.51*radial)+Vec{0,
                (crystal?0.80:0.78)+0.13*double((h>>16)&255)/255.0,0};
        }
        foot=pivot;
        crown=pivot+Vec{0,crystal?0.94:0.83,0};
    }

    template<class Face>
    void triangles(const Face& visitor,bool visibleOnly=false) const {
        if(bank){
            constexpr std::array<std::array<int,2>,8> ring={{{0,0},{1,0},{2,0},{2,1},{2,2},{1,2},{0,2},{0,1}}};
            for(int side=0;side<6;side++){
                if(visibleOnly && !(bank->visible&(1u<<side)))continue;
                const int axis=side/2,u=(axis+1)%3,v=(axis+2)%3;
                Cell p{1,1,1};p[axis]=2*(side%2);
                const Vec center=bank->at(p);
                for(int i=0;i<8;i++){
                    p[u]=ring[i][0];p[v]=ring[i][1];const Vec a=bank->at(p);
                    p[u]=ring[(i+1)%8][0];p[v]=ring[(i+1)%8][1];const Vec b=bank->at(p);
                    if(side%2)visitor(center,a,b);else visitor(center,b,a);
                }
            }
            return;
        }
        for(int i=0;i<8;i++){
            const int next=(i+1)%8;
            if(!visibleOnly || !grounded)visitor(foot,base[i],base[next]);
            visitor(base[i],waist[i],waist[next]);
            visitor(base[i],waist[next],base[next]);
            visitor(waist[i],shoulder[i],shoulder[next]);
            visitor(waist[i],shoulder[next],waist[next]);
            visitor(shoulder[i],crown,shoulder[next]);
        }
    }

    std::optional<TerrainFacetHit> ray(Vec origin,Vec direction,double begin,double end) const{
        std::optional<TerrainFacetHit> nearest;
        triangles([&](Vec a,Vec b,Vec c){
            const Vec ab=b-a,ac=c-a,perpendicular=cross(direction,ac);
            const double determinant=dot(ab,perpendicular);
            if(std::abs(determinant)<1e-10)return;
            const auto offset=origin-a;
            const double u=dot(offset,perpendicular)/determinant;
            if(u<-1e-9 || u>1+1e-9)return;
            const auto q=cross(offset,ab);
            const double v=dot(direction,q)/determinant;
            if(v<-1e-9 || u+v>1+1e-9)return;
            const double t=dot(ac,q)/determinant;
            if(t<begin || t>=end)return;
            const Vec face=cross(ab,ac);
            int axis=std::abs(face.x)>std::abs(face.y)?0:1;
            if(std::abs(face.z)>std::abs(face[axis]))axis=2;
            Cell normal{};
            normal[axis]=face[axis]>0?1:-1;
            const Vec outward=unit(face);
            nearest=TerrainFacetHit{t,normal,dot(outward,direction)>0?outward*-1:outward};
            end=t;
        });
        return nearest;
    }

    std::optional<Box> occupied(Box region) const{
        if(region.lo.x>=region.hi.x || region.lo.y>=region.hi.y || region.lo.z>=region.hi.z)return {};
        std::optional<Box> result;
        const auto include=[&](Vec point){
            if(!result){result=Box{point,point};return;}
            for(int axis=0;axis<3;axis++){
                result->lo[axis]=std::min(result->lo[axis],point[axis]);
                result->hi[axis]=std::max(result->hi[axis],point[axis]);
            }
        };
        triangles([&](Vec a,Vec b,Vec c){
            std::array<Vec,12> polygon{a,b,c};
            int count=3;
            for(int axis=0;axis<3 && count;axis++)for(double sign:{1.,-1.}){
                if(!count)break;
                std::array<Vec,12> next{};
                int size=0;
                const double plane=sign>0?region.lo[axis]:region.hi[axis];
                Vec previous=polygon[count-1];
                double previousSide=(previous[axis]-plane)*sign;
                for(int i=0;i<count;i++){
                    const Vec current=polygon[i];
                    const double currentSide=(current[axis]-plane)*sign;
                    if((currentSide<0)!=(previousSide<0))
                        next[size++]=previous+(current-previous)*(previousSide/(previousSide-currentSide));
                    if(currentSide>=0)next[size++]=current;
                    previous=current;previousSide=currentSide;
                }
                polygon=next;count=size;
            }
            for(int i=0;i<count;i++)include(polygon[i]);
        });
        for(double x:{region.lo.x,region.hi.x})for(double z:{region.lo.z,region.hi.z}){
            const auto top=ray({x,2,z},{0,-1,0},0,2.001);
            if(!top)continue;
            const auto bottom=ray({x,-1,z},{0,1,0},0,2.001);
            if(!bottom)continue;
            for(double y:{region.lo.y,region.hi.y})
                if(y>bottom->distance-1 && y<2-top->distance)include({x,y,z});
        }
        return result;
    }
};
