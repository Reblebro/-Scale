#pragma once
#include "render.hpp"
#include "surface-fracture.hpp"

// The ordinary chunk/LOD grid is bounded. Allocate masks only on slices that
// contain exposed faces and traverse the occupied rectangle in each slice.
class TerrainFaces {
    struct Slice {
        std::vector<std::uint8_t> material;
        int left{},right{},top{},bottom{};
    };
    Bounds bounds;
    Cell span;
    std::array<std::vector<Slice>,6> sides;
public:
    explicit TerrainFaces(Bounds portion):bounds(portion),span(portion.hi-portion.lo){
        for(int side=0;side<6;side++)sides[side].resize(span[side/2]);
    }
    void add(Cell p,int side,int material){
        const int n=side/2,u=(n+1)%3,v=(n+2)%3;
        const Cell local=p-bounds.lo;
        auto& slice=sides[side][local[n]];
        const int i=local[u],j=local[v];
        if(slice.material.empty()){
            slice.material.resize(std::size_t(span[u])*span[v]);
            slice.left=i;slice.right=i;slice.top=j;slice.bottom=j;
        }else{
            slice.left=std::min(slice.left,i);slice.right=std::max(slice.right,i);
            slice.top=std::min(slice.top,j);slice.bottom=std::max(slice.bottom,j);
        }
        slice.material[j*span[u]+i]=static_cast<std::uint8_t>(material);
    }
    void draw(Geometry& geometry,Cell seed,double spacing=1.0,
              float minTone=0.88f,float variation=0.14f,float green=1.0f,int maxWidth=64){
        for(int side=0;side<6;side++){
            const int axis=side/2,u=(axis+1)%3,v=(axis+2)%3;
            for(int n=0;n<span[axis];n++){
                auto& slice=sides[side][n];
                if(slice.material.empty())continue;
                const int stride=span[u];
                for(int j=slice.top;j<=slice.bottom;j++)for(int i=slice.left;i<=slice.right;i++){
                    const auto id=slice.material[j*stride+i];
                    if(!id)continue;
                    const int right=std::min(i+maxWidth,slice.right+1);
                    int end=i+1;
                    while(end<right && slice.material[j*stride+end]==id)++end;
                    const int lower=std::min(j+maxWidth,slice.bottom+1);
                    int bottom=j+1;
                    for(;bottom<lower;bottom++){
                        const auto row=slice.material.begin()+bottom*stride+i;
                        if(!std::all_of(row,row+(end-i),[id](auto cell){return cell==id;}))break;
                    }
                    for(int row=j+1;row<bottom;row++)
                        std::fill_n(slice.material.begin()+row*stride+i,end-i,0);
                    Cell start=bounds.lo;
                    start[axis]+=n;start[u]+=i;start[v]+=j;
                    Vec a=start.v();
                    if(side%2)a[axis]+=1;
                    Vec b=a,c=a,d=a;
                    b[u]+=end-i;c[u]+=end-i;c[v]+=bottom-j;d[v]+=bottom-j;
                    const auto key=CellHash{}(start+seed+Cell{0,0,side});
                    const float tone=minTone+float(key&63)*(variation/63);
                    const Color color{tone,tone*green,tone};
                    const Vec worldOrigin=seed.v()*spacing;
                    if(side%2)terrain::chippedQuad(geometry,a*spacing,b*spacing,c*spacing,d*spacing,
                        side,id,color,float(1/spacing),worldOrigin);
                    else terrain::chippedQuad(geometry,a*spacing,d*spacing,c*spacing,b*spacing,
                        side,id,color,float(1/spacing),worldOrigin);
                    i=end-1;
                }
                slice=Slice{};
            }
        }
    }
};
