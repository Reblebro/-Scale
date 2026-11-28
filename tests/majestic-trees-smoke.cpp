#include "../src/natural-world.hpp"
#include "../src/world.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>

int main(){
    constexpr std::uint64_t seed=62108432;
    int tested=0,rooted=0,outerRoots=0,crownVariety=0,unclipped=0;
    for(int iz=-15;iz<15;++iz)for(int ix=-15;ix<15;++ix){
        auto t=natural::treeAtGrid(ix,iz,seed);
        if(!t)continue;
        ++tested;
        assert(t->base+t->height+5<=ForestWorldHeight-2);
        assert(natural::treeMatter(*t,t->x,t->base+2,t->z)==2);
        double lo=std::numeric_limits<double>::infinity(), hi=-lo;
        for(const auto& b:t->boughs){lo=std::min(lo,b.y);hi=std::max(hi,b.y);}
        if(hi-lo>4.0)++crownVariety;
        int ring=0,foot=0;
        for(int dz=-8;dz<=8;++dz)for(int dx=-8;dx<=8;++dx){
            const double r=std::hypot(double(dx),double(dz));
            const int wx=t->x+dx,wz=t->z+dz;
            const double ground=natural::surfaceHeight(wx+0.5,wz+0.5,seed);
            for(int y=int(std::floor(ground))-2;y<=int(std::ceil(ground))+1;++y){
                if(!natural::rootMatter(*t,wx,y,wz,ground))continue;
                if(r>=2.8 && r<=7.5)++ring;
                if(r<2.2)++foot;
            }
        }
        if(ring>0)++rooted;
        outerRoots+=ring;
        assert(foot>0 && ring>5);
        for(int y=ForestWorldHeight-4;y<=ForestWorldHeight-2;++y)
            if(natural::treeMatter(*t,t->x,y,t->z)==1)++unclipped;
    }
    assert(tested>=150 && rooted==tested && crownVariety>tested*8/10);
    std::cout<<"PASS majestic canopy / roots: "<<tested<<" trees, "<<rooted
             <<" ground-hugging root systems, "<<outerRoots<<" root cells, "
             <<crownVariety<<" staggered crowns, "<<unclipped<<" top leaves\n";
}
