#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <numbers>
#include <optional>

// Runtime column height is independent of the original on-disk row format.
constexpr int ArchiveHeight=64;
constexpr int ForestWorldHeight=96;
constexpr double Pi=std::numbers::pi;
struct Vec {
    double x{},y{},z{};
    bool operator==(const Vec&) const=default;
    double operator[](int i) const { return i==0?x:i==1?y:z; }
    double& operator[](int i) { return i==0?x:i==1?y:z; }
    Vec operator+(Vec a) const { return {x+a.x,y+a.y,z+a.z}; }
    Vec operator-(Vec a) const { return {x-a.x,y-a.y,z-a.z}; }
    Vec operator*(double j) const { return {x*j,y*j,z*j}; }
    Vec operator/(double j) const { return *this*(1/j); }
    Vec& operator+=(Vec a) { return *this=*this+a; }
};
inline double dot(Vec a,Vec b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec cross(Vec a,Vec b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline double length(Vec a) { return std::sqrt(dot(a,a)); }
inline Vec unit(Vec a) { double l=length(a); return l > 1e-12 ? a/l : Vec{}; }
struct Cell {
    int x{},y{},z{};
    int operator[](int i) const { return i==0?x:i==1?y:z; }
    int& operator[](int i) { return i==0?x:i==1?y:z; }
    Cell operator+(Cell p) const { return {x+p.x,y+p.y,z+p.z}; }
    Cell operator-(Cell p) const { return {x-p.x,y-p.y,z-p.z}; }
    Cell operator*(int n) const { return {x*n,y*n,z*n}; }
    auto operator<=>(const Cell&) const=default;
    Vec v() const { return {double(x),double(y),double(z)}; }
};
inline int divFloor(int n,int d) { int q=n/d; return q-(n%d<0); }
inline Cell parent(Cell c,int n=4) { return {divFloor(c.x,n),divFloor(c.y,n),divFloor(c.z,n)}; }
inline Cell floorCell(Vec v) { return {int(std::floor(v.x)),int(std::floor(v.y)),int(std::floor(v.z))}; }
inline std::uint32_t mix(std::uint32_t v) {
    v^=v>>16; v*=0x7feb352du; v^=v>>15; v*=0x846ca68bu; return v^(v>>16);
}
struct CellHash {
    std::size_t operator()(Cell c) const {
        return mix(std::uint32_t(c.x)*0x8da6b343u ^ std::uint32_t(c.y)*0xd8163841u ^ std::uint32_t(c.z)*0xcb1ab31fu);
    }
};
inline constexpr std::array<Cell,6> Sides={{{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}}};
struct Bounds {
    Cell lo,hi;
    bool contains(Cell c) const { return c.x>=lo.x && c.x<hi.x && c.y>=lo.y && c.y<hi.y && c.z>=lo.z && c.z<hi.z; }
};

struct Box {
    Vec lo{},hi{};
    bool intersects(const Box& q) const {
        return lo.x<q.hi.x && hi.x>q.lo.x && lo.y<q.hi.y && hi.y>q.lo.y && lo.z<q.hi.z && hi.z>q.lo.z;
    }
};
struct RayBox { double at; Cell normal; };
inline std::optional<RayBox> intersectRay(Vec origin,Vec ray,Box box,double reach) {
    double enter=0,leave=reach;
    Cell normal{};
    for(int axis=0;axis<3;axis++){
        if(std::abs(ray[axis])<1e-10){
            if(origin[axis]<box.lo[axis] || origin[axis]>=box.hi[axis])return {};
            continue;
        }
        const int sign=ray[axis]>0?1:-1;
        const double near=((sign>0?box.lo[axis]:box.hi[axis])-origin[axis])/ray[axis];
        const double far=((sign>0?box.hi[axis]:box.lo[axis])-origin[axis])/ray[axis];
        if(near>=enter){enter=near;normal={};normal[axis]=-sign;}
        leave=std::min(leave,far);
        if(leave-enter<1e-9)return {};
    }
    return RayBox{enter,normal};
}

inline double rayBox(Vec origin,Vec dir,Vec lower,Vec upper,double reach) {
    double near=0,far=reach;
    for(int i=0;i<3;i++) {
        if(std::abs(dir[i])<1e-12) {
            if(origin[i]<lower[i] || origin[i]>upper[i])return reach+1;
            continue;
        }
        double a=(lower[i]-origin[i])/dir[i],b=(upper[i]-origin[i])/dir[i];
        near=std::max(near,std::min(a,b));far=std::min(far,std::max(a,b));
        if(far<near)return reach+1;
    }
    return near;
}
inline Vec viewDirection(double yaw,double pitch) {
    return {std::sin(yaw)*std::cos(pitch),std::sin(pitch),-std::cos(yaw)*std::cos(pitch)};
}
