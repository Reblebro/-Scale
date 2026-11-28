#include "game.hpp"
#include "platform.hpp"
#include "mesh-faces.hpp"
#include "arboreal-mesh.hpp"
#include "edited-mesh.hpp"
#include "atmosphere.hpp"
#include "trail-world.hpp"
#include <fstream>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <set>
#include <stdexcept>
#include <utility>

namespace {
constexpr double edgeOhms=0.32,lampOhms=8,houseOhms=80;
void trailFixture(Geometry& posts,Geometry& flames,Vec p){
    const Color iron{.48f,.39f,.29f},wood{.62f,.47f,.31f},amber{1.f,.72f,.31f};
    posts.tube(p-Vec{0,.20,0},p+Vec{0,1.65,0},.095,Timber,wood,8);
    posts.tube(p+Vec{0,.04,0},p+Vec{0,.23,0},.137,Steel,iron,8);
    posts.tube(p+Vec{0,1.35,0},p+Vec{0,1.43,0},.119,Steel,iron,8);
    posts.box(p+Vec{-.21,1.59,-.21},p+Vec{.21,1.67,.21},Steel,iron);
    posts.box(p+Vec{-.23,2.12,-.23},p+Vec{.23,2.21,.23},Steel,iron);
    const Vec peak=p+Vec{0,2.39,0};
    const std::array<Vec,4> eave={p+Vec{-.26,2.20,-.26},p+Vec{-.26,2.20,.26},p+Vec{.26,2.20,.26},p+Vec{.26,2.20,-.26}};
    for(int i=0;i<4;++i)posts.triangle(eave[i],eave[(i+1)%4],peak,Steel,iron);
    posts.tube(p+Vec{0,2.37,0},p+Vec{0,2.46,0},.044,Steel,iron,6);
    for(double x:{-.18,.18})for(double z:{-.18,.18})
        posts.tube(p+Vec{x,1.66,z},p+Vec{x,2.14,z},.022,Steel,iron,5);
    flames.box(p+Vec{-.115,1.72,-.115},p+Vec{.115,2.08,.115},Lamp,amber);
}


constexpr std::array placement={Dirt,Stone,Timber,Clay,Cable,Lamp,Switch,CopperOre,IronOre,Coal,Lodestone,Sand,Quartz,Quarry,North,Fabricator,Door,Seat,Lantern,Plank,RoofTile,PrismPress,ResourceWell,HouseBuilder,CampTile,MeadowGate,QuartzGate,CrankGenerator,SolarPanel};
constexpr std::array materialsList={Dirt,Stone,Timber,Clay,CopperOre,IronOre,Coal,Lodestone,Sand,Quartz};
constexpr std::array buildPalette={Dirt,Stone,Timber,Clay,CopperOre,IronOre,Coal,Lodestone,Sand,Quartz,
    Door,Seat,Lantern,Plank,RoofTile,Quarry,North,Fabricator,PrismPress,ResourceWell,HouseBuilder,CampTile,MeadowGate,QuartzGate,CrankGenerator,SolarPanel};
bool mounted(int id){return (id>=Cable && id<=Switch) || id==Quarry || id==North || id==Fabricator || furniture(id) || id==PrismPress || id==ResourceWell || id==HouseBuilder || id==CrankGenerator || id==SolarPanel;}
constexpr double quarryOhms=4.0;
constexpr double machineRange=30.0;
constexpr int quarryRadius=6;
bool workshopRecipe(int id){return id==Quarry || id==ResourceWell;}
bool circuitPart(int id){return id!=North && id!=Fabricator && id!=PrismPress && id!=ResourceWell && !furniture(id);}
bool machineRecipe(int id){
    return workshopRecipe(id) || id==SolarPanel || id==North || id==Generator || id==Cable || id==Lamp ||
        id==Switch || id==Fabricator || id==PrismPress || id==HouseBuilder || id==PowerPack;
}
bool prismRecipe(int id){
    return id==PulseCrystal || id==PulseRounds || id==PulsePistol || id==PulseRifle;
}
bool terrainEditsAllowed(int id) {
    return id==Air || sculptable(id);
}
constexpr Color mint{0.55f,0.94f,0.78f},white{0.91f,0.95f,0.94f},
    soft{0.65f,0.75f,0.74f},amber{0.98f,0.76f,0.41f},
    dark{0.088f,0.064f,0.043f,0.88f},rule{0.58f,0.46f,0.28f,0.93f};
struct Recipe {const char* label; int makes,count; bool kiln,power; std::vector<std::pair<int,int>> cost; };
const auto recipes=std::to_array<Recipe>({
    {"Stone pick",Pick,1,false,false,{{Timber,2},{Stone,3}}},
    {"Build kiln",Kiln,1,false,false,{{Clay,4},{Stone,4}}},
    {"Smelt copper",Copper,1,true,false,{{CopperOre,2},{Coal,1}}},
    {"Smelt iron",Iron,1,true,false,{{IronOre,2},{Coal,1}}},
    {"Wind coil",Coil,1,false,false,{{Copper,2},{Timber,1}}},
    {"Forge crank",Crank,1,false,false,{{Iron,2},{Timber,1}}},
    {"Shape magnet",Magnet,1,false,false,{{Lodestone,2}}},
    {"Assemble generator",Generator,1,false,false,{{Coil,1},{Crank,1},{Magnet,1},{Timber,2}}},
    {"Insulated cable",Cable,4,true,true,{{Copper,1},{Clay,1}}},
    {"Field lamp",Lamp,1,true,true,{{Copper,1},{Clay,1}}},
    {"Switch",Switch,1,true,true,{{Copper,1},{Iron,1}}},
    {"Iron pick",IronPick,1,true,false,{{Iron,3},{Timber,2}}},
    {"Machine crafter",Fabricator,1,true,true,{{Iron,6},{Timber,4}}},
    {"Quarry",Quarry,1,true,true,{{Iron,100}}},
    {"North",North,1,true,true,{{Iron,6},{Quartz,2}}},
    {"Prism press",PrismPress,1,true,true,{{Iron,4},{Quartz,2},{Timber,3}}},
    {"Pulse pistol",PulsePistol,1,true,true,{{Iron,5},{Copper,2},{Quartz,1}}},
    {"Pulse crystal",PulseCrystal,1,true,true,{{Quartz,1},{Copper,1}}},
    {"Pulse rounds (five)",PulseRounds,5,true,true,{{Iron,1},{PulseCrystal,1}}},
    // Retain the old recipe index for existing tests and saved item IDs.
    {"Retired optic recipe",QuartzSight,0,false,false,{}},
    {"Wearable battery",PowerPack,1,true,true,{{Iron,3},{Copper,3},{Quartz,1}}},
    {"Door",Door,1,false,false,{{Timber,2}}},
    {"Chair",Seat,1,false,false,{{Timber,2}}},
    {"Lantern",Lantern,1,true,false,{{Copper,1},{Coal,1}}},
    {"Planks (four)",Plank,4,false,false,{{Timber,1}}},
    {"Roof tiles (four)",RoofTile,4,true,false,{{Clay,2}}},
    {"Temper stone pick",Tempering,1,true,false,{{Iron,3},{Coal,1}}},
    {"Pulse rifle",PulseRifle,1,true,true,{{Iron,9},{Copper,4},{Quartz,2},{Timber,2}}},
    {"Resource generator",ResourceWell,1,false,false,{{Iron,5}}},
    {"House Builder",HouseBuilder,1,true,true,{{Iron,3},{Copper,1},{Timber,4}}},
    {"Solar panel",SolarPanel,1,true,false,{{Quartz,4},{Copper,3},{Iron,3},{Timber,2}}},
});
std::string number(double value,int digits) {
    std::ostringstream output;
    output<<std::fixed<<std::setprecision(digits)<<value;
    return output.str();
}
std::string amount(std::int64_t units) {
    return std::to_string(units/FullBlock);
}
std::string meters(double distance) {
    if(distance>=1)return number(distance,2)+" m";
    if(distance>=0.01)return number(distance*1e2,2)+" cm";
    if(distance>=0.001)return number(distance*1e3,2)+" mm";
    if(distance>=1e-6)return number(distance*1e6,2)+" um";
    if(distance>=1e-9)return number(distance*1e9,2)+" nm";
    if(distance>=1e-12)return number(distance*1e12,2)+" pm";
    return number(distance*1e15,2)+" fm";
}
bool sameBounds(Bounds a,Bounds b) {return a.lo==b.lo && a.hi==b.hi;}

template<class Fits>
double fitZoom(double current,double desired,const Fits& fits) {
    if(fits(desired))return desired;
    if(desired>=current)return current;
    // Keep the sole fixed and stop growing at the largest hull that still fits.
    double blocked=desired,clear=current;
    for(int step=0;step<13;step++){
        const double middle=(blocked+clear)*0.5;
        if(fits(middle))clear=middle;
        else blocked=middle;
    }
    return clear;
}

Electrical solveNetwork(const std::unordered_map<Cell,Device,CellHash>& devices,Cell port) {
    std::vector<Cell> nodes{port};
    std::unordered_map<Cell,int,CellHash> indices{{port,0}};
    std::vector<std::vector<int>> links;
    for(std::size_t i=0;i<nodes.size();i++) {
        const Cell here=nodes[i];
        links.emplace_back();
        for(const auto& direction:Sides) {
            const Cell next=here+direction;
            auto known=indices.find(next);
            if(known==indices.end()) {
                auto found=devices.find(next);
                if(found==devices.end() || !circuitPart(found->second.id) ||
                   (found->second.id==Switch && !found->second.closed))continue;
                known=indices.emplace(next,int(nodes.size())).first;
                nodes.push_back(next);
            }
            links.back().push_back(known->second);
        }
    }

    Electrical result;
    const int count=int(nodes.size())-1;
    result.connected=count;
    std::vector<double> diagonal(count),residual(count),direction(count),voltage(count),output(count);
    std::vector<int> loads;
    const double edge=1/edgeOhms;
    for(int i=0;i<count;i++) {
        const auto& device=devices.at(nodes[i+1]);
        const double conductance=device.id==Lamp?1/lampOhms:device.id==Quarry && device.closed?1/quarryOhms:device.id==HouseBuilder?1/houseOhms:0;
        if(conductance)loads.push_back(i);
        diagonal[i]=links[i+1].size()*edge+conductance;
        residual[i]=direction[i]=std::find(links[i+1].begin(),links[i+1].end(),0)==links[i+1].end()?0:edge;
    }
    if(loads.empty())return result;
    double magnitude=std::inner_product(residual.begin(),residual.end(),residual.begin(),0.0);
    for(int step=0;step<count*2 && magnitude>1e-20;step++) {
        double projection=0;
        for(int i=0;i<count;i++) {
            double value=diagonal[i]*direction[i];
            for(int neighbor:links[i+1])if(neighbor) value-=edge*direction[neighbor-1];
            output[i]=value;
            projection+=direction[i]*value;
        }
        if(projection<=0)break;
        const double alpha=magnitude/projection;
        double next=0;
        for(int i=0;i<count;i++) {
            voltage[i]+=alpha*direction[i];residual[i]-=alpha*output[i];next+=residual[i]*residual[i];
        }
        const double beta=next/magnitude;
        for(int i=0;i<count;i++)direction[i]=residual[i]+beta*direction[i];
        magnitude=next;
    }
    for(int i:loads) {
        const double ratio=std::clamp(voltage[i],0.0,1.0);
        const int id=devices.at(nodes[i+1]).id;
        auto& inventory=id==Quarry?result.quarries:id==HouseBuilder?result.houses:result.lamps;
        inventory.emplace(nodes[i+1],ratio);
        result.conductance+=ratio/(id==Quarry?quarryOhms:id==HouseBuilder?houseOhms:lampOhms);
    }
    return result;
}


Vec deviceCenter(const Device& dev) { return Vec{0.5,0.5,0.5}-dev.normal.v()*0.39; }

Box geometryBounds(const Geometry& mesh) {
    Box result{{1e9,1e9,1e9},{-1e9,-1e9,-1e9}};
    for(const auto& vertex:mesh.points){
        const Vec p{vertex.x,vertex.y,vertex.z};
        for(int axis=0;axis<3;axis++){
            result.lo[axis]=std::min(result.lo[axis],p[axis]);
            result.hi[axis]=std::max(result.hi[axis],p[axis]);
        }
    }
    return result;
}
}

void GeneratorState::advance(double dt,double load) {
    const double drive=std::clamp(1.8*(rpm*Pi/30-speed),-8.0,8.0);
    const double g=load+(test?1.0/3:0);
    const double loss=1+g*0.055*turns;
    const double exchange=turns*Pi*0.64*0.64*field*std::sin(angle);
    speed+=dt*(drive-exchange*exchange*speed*g/loss-0.025*speed)/0.18;
    angle=std::fmod(angle+speed*dt,2*Pi);
    voltage=turns*Pi*0.64*0.64*field*std::sin(angle)*speed/loss;
    current=voltage*g;
    gridWatts=voltage*voltage*load;
    rms2+=(voltage*voltage-rms2)*(1-std::exp(-dt/0.32));
}

Game::Game(Renderer& draw,std::filesystem::path data,std::filesystem::path save,bool read,bool modern)
    :renderer(draw),world(std::move(data)),saveFile(std::move(save)),modernWorld(modern) {
    if(!world.validAssets())throw std::runtime_error("Could not find the complete 1 GiB world. Extract the entire archive.");
    if(!saveFile.empty()){
        std::ifstream input(saveFile.parent_path()/"seed.txt");
        std::uint64_t seed;
        if(input>>seed){world.setSeed(seed);legacyRig=false;}
    }
    if(modernWorld){
        if(!world.seed)world.setSeed(62108432);
        world.sculptedTerrain=true;
        legacyRig=false;
        lootState^=world.seed.value_or(0);
        if(!read || saveFile.empty() || !std::filesystem::exists(saveFile)){
            devices.emplace(Cell{-2,29,-4},Device{North});
            bag[PulseRifle]=FullBlock;
            bag[PulseRounds]=6*FullBlock;
            weapon=PulseRifle;
        }
    }
    if(read)load();
    if(modernWorld){
        night=solarcycle::at(solarClock).night;
        zoom=goalZoom=std::clamp(std::round(zoom),0.0,double(FineDepth));
        innerZoom=innerGoal=inFortress()?0.:std::clamp(std::round(innerZoom),0.0,6.0);
        precision=surfaceTier();
    }
    world.stream(floorCell(feet),2);
    recoverPosition();
    makeRig();
    makeExpeditionModels();
    if(modernWorld){
        renderer.setNorthTexture();
        makeGoblinModels();
    }
    refreshGrid();
}
Game::~Game(){save();}
double Game::body() const {return std::pow(0.25,zoom);}
double Game::timeRate() const {
    if(level>=0 && !surfaceFortress)return std::pow(0.28,level+2+innerZoom);
    return std::pow(0.43,zoom);
}
Vec Game::camera() const {
    if(level>=0)return innerFeet+Vec{0,(crouched?0.83:EyeHeight)*innerSize(),0};
    return feet+Vec{0,(crouched?0.83:EyeHeight)*body(),0};
}
double Game::innerSize() const {return std::pow(modernWorld?0.25:0.42,innerZoom);}
int Game::surfaceTier() const {return modernWorld?int(zoom):mobility::surfaceDetail(zoom);}
int Game::realmTier() const {return modernWorld?int(innerZoom):mobility::realmDetail(innerZoom);}
void Game::message(std::string value){status=std::move(value);statusTime=3.2;}
bool Game::camp() const {return level<0 && std::hypot(feet.x-0.5,feet.z-2)<24 && feet.y>=25 && feet.y<39;}
bool Game::atGenerator() const {return level<0 && legacyRig && built && length(feet-Rig)<7;}

void Game::look(double x,double y) {
    if(!wantsCapture())return;
    yaw=std::remainder(yaw+x*0.0025*sensitivity,2*Pi);
    pitch=std::clamp(pitch-y*0.0025*sensitivity,-1.54,1.54);
}
void Game::wheel(double amount) {
    if(!wantsCapture())return;
    if(modernWorld){
        wheelCarry+=amount;
        amount=std::trunc(wheelCarry);
        if(amount==0)return;
        wheelCarry-=amount;
        if(level>=0){
            if(inFortress() && surfaceFortress){
                if(amount<0){ascend();goalZoom=std::clamp(double(FineDepth)+amount,0.,double(FineDepth-1));}
                else message("You are at the final scale");
                return;
            }
            if(amount<0 && innerGoal==0 && length(innerFeet-micro::Arrival)<3){ascend();return;}
            if(inFortress()){if(amount>0)message("You are at the final scale");return;}
            innerGoal=std::clamp(innerGoal+amount,0.0,6.0);
        }else goalZoom=std::clamp(goalZoom+amount,0.0,double(FineDepth));
        return;
    }
    if(amount<0 && portalZoom){
        portalZoom=false;
        if(level>=0)innerGoal=0.65;
        else goalZoom=2.45;
        return;
    }
    if(level>=0){
        if(amount<0 && innerGoal<=0.01 && length(innerFeet-micro::Arrival)<3){ascend();return;}
        const double previous=innerGoal;
        innerGoal=std::clamp(innerGoal+amount*mobility::RealmStep,0.0,mobility::RealmZoom);
        if(previous<mobility::RealmZoom && innerGoal==mobility::RealmZoom)
            message("Smallest scale reached.");
        if(amount>0)descend(false);
    }else {
        const double previous=goalZoom;
        goalZoom=std::clamp(goalZoom+amount*mobility::SurfaceStep,0.0,mobility::SurfaceZoom);
        if(previous<mobility::SurfaceZoom && goalZoom==mobility::SurfaceZoom)
            message("Smallest scale reached.");
        if(amount>0)descend(false);
    }
}
void Game::movement(double dt,const Motion& controls) {
    stance(controls.crouch);
    if(level>=0){moveMicro(dt,controls);return;}
    Vec planar={std::sin(yaw),0,-std::cos(yaw)};
    Vec right={std::cos(yaw),0,std::sin(yaw)};
    if(overlapsField(feet,body()))recoverPosition();
    const double request=std::lerp(zoom,goalZoom,1-std::exp(-11*dt));
    const double nextZoom=modernWorld?goalZoom:std::abs(request-goalZoom)<0.001?goalZoom:request;
    const auto fits=[&](double value){return !overlapsField(feet,mobility::tierSize(value));};
    if(modernWorld)zoom=mobility::fitTier(int(zoom),int(goalZoom),fits);
    else zoom=fitZoom(zoom,nextZoom,fits);
    if(nextZoom<zoom-1e-5 && statusTime<=0)message("Move clear of the material to grow");
    precision=surfaceTier();
    if(modernWorld && precision==FineDepth && goalZoom==FineDepth){enterSurfaceFortress();return;}
    const double scale=body();
    Vec wish=planar*controls.ahead+right*controls.across;
    if(flying)wish.y=controls.rise;
    moveInput(dt,wish);
    const auto speed=modernWorld?mobility::scaledSpeed:mobility::speed;
    const Vec move=smoothInput*(dt*speed(scale,false,flying,controls.crouch,controls.brisk));
    const double stepHeight=std::clamp(1.0001/subdivisions(precision),scale*0.32,scale*1.35);
    mobility::advance(feet,scale,move,dt,stepHeight,modernWorld?scale:std::max(scale,0.0018),flying,controls.jump,
        previousJump,grounded,vertical,[&](Vec p,double hull,int axis,double distance){
            return sweepField(p,hull,axis,distance);
        });
}

void Game::recoverPosition(){
    if(level>=0){
        auto& room=layer(family,level);
        if(!room.overlaps(innerFeet,innerSize(),hullHeight()))return;
        innerFeet=micro::Arrival;
        innerGoal=innerZoom=0;
        vertical.reset();
        message("Moved to the entrance of this layer");
        return;
    }
    const double scale=body();
    if(!overlapsField(feet,scale))return;
    const Vec original=feet;
    for(double distance: {0.001,0.005,0.02,0.05,0.12,0.26,0.51,1.02,2.04,4.08}){
        for(Vec offset:{Vec{0,1,0},Vec{1,0,0},Vec{-1,0,0},Vec{0,0,1},Vec{0,0,-1},
                        Vec{1,1,0},Vec{-1,1,0},Vec{0,1,1},Vec{0,1,-1}}){
            const Vec candidate=original+offset*distance;
            if(overlapsField(candidate,scale))continue;
            feet=candidate;
            goalZoom=zoom;vertical.reset();
            return;
        }
    }
    feet={0.5,29,-7.1};zoom=goalZoom=0;precision=0;vertical.reset();
    world.stream(floorCell(feet),2);
    message("Returned to camp from an enclosed spot");
}

void Game::focus() {
    gateTarget.reset();
    if(level>=0){
        deviceTarget.reset();
        const int detailLevel=realmTier();
        double reach=4.8*std::sqrt(innerSize());
        if(detailLevel)reach=std::min(reach,3.0/subdivisions(detailLevel-1));
        target=layer(family,level).ray(camera(),viewDirection(yaw,pitch),reach,detailLevel);
        return;
    }
    const Vec eye=camera(),dir=viewDirection(yaw,pitch);
    double reach=5.0*std::pow(body(),0.65);
    if(precision)reach=std::min(reach,(precision>=3?2.0:3.0)/subdivisions(precision-1));
    target=world.ray(eye,dir,precision,reach);
    deviceTarget.reset();
    if(precision>0)return;
    double best=target?target->distance:reach;
    for(int i=0;i<int(gates.size());i++)if(gates[i]){
        const Vec anchor=*gates[i];
        const double hit=rayBox(eye,dir,anchor-Vec{0.61,0,0.61},anchor+Vec{0.61,1.85,0.61},best);
        if(hit<best){best=hit;gateTarget=i;}
    }
    auto consider=[&](Cell cell,const Device& device) {
        if(furniture(device.id)){
            const auto shape=expedition::furnitureVolume(device.id,cell.v(),!device.closed);
            const double hit=rayBox(eye,dir,shape.lo,shape.hi,best);
            if(hit<best){best=hit;deviceTarget=cell;gateTarget.reset();}
            return;
        }
        Vec center=cell.v()+deviceCenter(device);
        if(device.id==North)center=northOrigin(cell)+Vec{0,1.2,0};
        Vec r;
        if(device.id==Cable)for(int i=0;i<3;i++)r[i]=device.normal[i]?0.115:0.32;
        else {
            const double height=device.id==North?2.4:(workshopRecipe(device.id)||device.id==Fabricator||device.id==HouseBuilder||device.id==Lamp||device.id==SolarPanel||device.id==CrankGenerator)?0.92:0.52;
            if(device.id!=North)center.y=cell.y+height/2;
            r={device.id==North?.53:.34,height/2,device.id==North?.44:.34};
        }
        double hit=rayBox(eye,dir,center-r,center+r,best);
        if(hit<best){best=hit;deviceTarget=cell;gateTarget.reset();}
    };
    if(built && legacyRig)consider(Port,{Socket,true,{0,1,0}});
    consider(CampStation,{Fabricator,true,{0,1,0}});
    consider(CraftTable,{Fabricator,true,{0,1,0}});
    for(const auto& [cell,device]:devices){
        const Vec offset=(device.id==North?northOrigin(cell):cell.v())-eye;
        if(dot(offset,offset)<(reach+2)*(reach+2))consider(cell,device);
    }
}
bool Game::shovel(int id,bool deposit){
    const int tier=level<0?precision:realmTier();
    if(!sculptable(id))return false;
    if(!deposit && id==CampTile && !bag[IronPick]){message("Camp flooring requires an iron pick");return true;}
    if(!deposit && level<0 && requiresPick(id) && !bag[IronPick] && bag[Pick]<FullBlock){
        message("Craft a stone pick first"+keyHint(platform::C));return true;
    }
    const int material=harvestMaterial(id);
    if(deposit && bag[material]<FullBlock){message(std::string("Gather more ")+materialName(material));return true;}
    const Vec point=camera()+viewDirection(yaw,pitch)*target->distance;
    const Vec normal=length(target->surfaceNormal)>0.5?target->surfaceNormal:target->normal.v();
    const auto player=World::playerBox(level<0?feet:innerFeet,level<0?body():innerSize(),hullHeight());
    const std::int64_t budget=deposit?std::min(bag[material],FullBlock):FullBlock;
    const auto allowed=[&](Leaf leaf){
        const auto root=parent(leaf.position,subdivisions(leaf.level));
        if(deposit && player.intersects(leaf.bounds()))return false;
        for(const auto cell:{root,root+Cell{0,1,0}}){
            if(cell==CampStation || cell==CraftTable || (legacyRig && built && cell==Port) ||
               std::any_of(gates.begin(),gates.end(),[&](const auto& g){return g && floorCell(*g)==cell;}))
                return false;
        }
        if(devices.contains(root))return false;
        for(const auto side:Sides){
            const auto part=devices.find(root+side);
            if(part!=devices.end() && part->second.normal==side)return false;
        }
        return true;
    };
    const auto moved=level<0?world.wholeStroke(point,tier,material,deposit,budget,allowed,normal):
        layer(family,level).shovel(point,tier,material,deposit,budget,player,normal);
    if(moved){
        bag[material]+=deposit?-moved:moved;
        const bool broke=!deposit && level<0 && requiresPick(id) && !bag[IronPick] && bag[Pick] && --pickUses<=0;
        if(broke)bag[Pick]=0;
        message(std::string(deposit?"Packed ":"Scooped ")+amount(moved)+" "+materialName(material)+(broke?" | Stone pick broke":""));
        focus();
    }else message(deposit?"Not enough clear space for one unit":"Not enough connected material for one unit; shrink to dig smaller pieces");
    return true;
}
void Game::collect(bool exact) {
    focus();
    if(level>=0){
        if(!target)return;
        const auto hit=*target;
        if(!exact && shovel(hit.id,false))return;
        if(protectedMaterial(hit.id) && !(hit.id==CampTile && bag[IronPick])){
            message(hit.id==CampTile?"Camp flooring requires an iron pick":"That surface is fixed");return;
        }
        auto& area=layer(family,level);
        const int local=furniture(hit.id)?0:realmTier();
        const Cell selected=parent(hit.cell,subdivisions(hit.level-local));
        if(!furniture(hit.id) && sculptable(hit.id)){
            const auto pieces=area.blocks().composition(local,selected);
            const int item=harvestMaterial(hit.id);
            MatterVolume matching=0;
            for(int m=1;m<int(pieces.size());++m)if(harvestMaterial(m)==item)matching+=pieces[m];
            if(matching!=volume(local)){message("Shrink to recover this fragment, or scoop connected material");return;}
        }
        if(area.setDetail(local,selected,Air,bag[IronPick]>0)){
            const int item=furniture(hit.id)?furnitureBase(hit.id):harvestMaterial(hit.id);
            bag[item]+=FullBlock;
            message(std::string("+1 ")+materialName(item));
            focus();
        }
        return;
    }
    if(gateTarget){
        if(!bag[IronPick]){message("Recovering a portal requires an iron pick");return;}
        const int i=*gateTarget;
        bag[i==0?MeadowGate:QuartzGate]+=FullBlock;
        gates[i].reset();gateTarget.reset();message("Portal recovered");return;
    }
    if(deviceTarget){
        auto found=devices.find(*deviceTarget);
        if(found==devices.end()){
            message(*deviceTarget==CampStation?"Camp Machine Crafter":*deviceTarget==CraftTable?"Crafting table":"Generator terminal");return;
        }
        bag[found->second.id]+=FullBlock;
        message(std::string("Recovered ")+materialName(found->second.id));
        devices.erase(found);refreshGrid();return;
    }
    if(!target)return;
    if(!exact && shovel(target->id,false))return;
    const int level=precision,id=target->id;
    const Cell cell=parent(target->cell,subdivisions(target->level-level));
    if(protectedMaterial(id) && !(id==CampTile && bag[IronPick])){message(id==CampTile?"Camp flooring requires an iron pick":"This barrier is fixed");return;}
    if(requiresPick(id) && bag[Pick]<FullBlock && !bag[IronPick]){message("Craft a stone pick first"+keyHint(platform::C));return;}
    const Cell root=parent(cell,subdivisions(level));
    {
        for(const auto& direction:Sides){
            const Cell housing=root+direction;
            if(direction==Cell{0,1,0} && (housing==CampStation || housing==CraftTable || std::any_of(gates.begin(),gates.end(),[&](const auto& gate){return gate && floorCell(*gate)==housing;}))){message("This block supports a station or portal");return;}
            if(legacyRig && built && housing==Port && direction==Cell{0,1,0}){message("That block supports the terminal");return;}
            auto found=devices.find(housing);
            if(found!=devices.end() && found->second.normal==direction){
                message("Recover the component mounted on this block first");return;
            }
        }
    }
    const auto materials=world.composition(level,cell);
    for(int mineral=1;mineral<int(materials.size());mineral++)if(materials[mineral]>0){
        if(protectedMaterial(mineral) && !(mineral==CampTile && bag[IronPick])){message("This section contains protected material");return;}
        if(requiresPick(mineral) && !bag[Pick] && !bag[IronPick]){message("Craft a stone pick first"+keyHint(platform::C));return;}
    }
    if(sculptable(id)){
        MatterVolume matching=0;
        for(int m=1;m<int(materials.size());++m)if(harvestMaterial(m)==harvestMaterial(id))matching+=materials[m];
        if(matching!=volume(level)){message("Shrink to recover this fragment, or scoop connected material");return;}
    }
    const auto removed=-world.apply({level,cell,Air});
    if(removed>0){
        const int item=harvestMaterial(id);
        const bool broken=!bag[IronPick] && bag[Pick] && --pickUses<=0;
        if(broken)bag[Pick]=0;
        bag[item]+=FullBlock;
        message(std::string("+1 ")+materialName(item)+(broken?" | Stone pick broke":""));
    }
}
void Game::place(bool exact) {
    focus();
    if(!target || deviceTarget || gateTarget || target->normal==Cell{})return;
    const int id=placement[selected];
    if(!exact && shovel(id,true))return;
    Cell cell=target->previous;
    const int level=this->level>=0?realmTier():precision;
    if(target->level!=level){
        const double size=1.0/subdivisions(target->level);
        auto point=camera()+viewDirection(yaw,pitch)*target->distance;
        for(int axis=0;axis<3;axis++)if(target->normal[axis]){
            point[axis]=(target->cell[axis]+(target->normal[axis]>0?1:0))*size+target->normal[axis]*size*1e-5;
        }
        cell=floorCell(point*subdivisions(level));
    }
    if(id==MeadowGate || id==QuartzGate){
        if(this->level>=0 || level || target->normal!=Cell{0,1,0} || bag[id]<FullBlock){
            message("Place portals on a clear surface at normal scale");return;
        }
        const int i=id==MeadowGate?0:1;
        const Vec anchor=cell.v()+Vec{0.5,0,0.5};
        const Box portal{anchor-Vec{0.6,0,0.6},anchor+Vec{0.6,1.85,0.6}};
        if(gates[i] || world.overlaps(anchor,1.5,1.24) || devices.contains(cell) || devices.contains(cell+Cell{0,1,0}) ||
            portal.intersects(World::playerBox(feet,body(),hullHeight())) ||
            (gates[1-i] && length(anchor-*gates[1-i])<3)){message("Portal needs a clear landing");return;}
        bag[id]-=FullBlock;gates[i]=anchor;message("Portal anchored");return;
    }
    if(this->level>=0){
        if((mounted(id) && (!furniture(id) || level>0)) || bag[id]<FullBlock){
            message("Select a block you own. Furniture is placed at realm scale.");
            return;
        }
        auto& area=layer(family,this->level);
        const auto root=parent(cell,subdivisions(level));
        if(!micro::within(root) || furniture(area.raw(root)) || area.blocks().sample(level,cell).amount)return;
        const double size=1.0/subdivisions(level);
        const auto lo=cell.v()*size;
        if(World::playerBox(innerFeet,innerSize(),hullHeight()).intersects({lo,lo+Vec{size,size,size}}))return;
        if(area.setDetail(level,cell,id)){bag[id]-=FullBlock;message(std::string("Placed ")+materialName(id));}
        return;
    }
    if(mounted(id)){
        if((!built && circuitPart(id) && id!=SolarPanel && id!=CrankGenerator) || level!=0 || bag[id]<FullBlock){message("Mount parts at normal scale");return;}
        if(id==North && (std::any_of(devices.begin(),devices.end(),[](const auto& item){return item.second.id==North;}) ||
            world.overlaps(cell.v()+Vec{0.5,0,0.5},1,2) ||
            World::playerBox(feet,body(),hullHeight()).intersects({cell.v(),cell.v()+Vec{1,2,1}}))){
            message("North needs a clear space");return;
        }
        if((legacyRig && built && cell==Port) || cell==CampStation || cell==CraftTable || devices.contains(cell) || world.sample(0,cell).solid())return;
        const auto normal=target->normal;
        if(id!=Cable && normal!=Cell{0,1,0}){message("Mount this part on top of a solid block");return;}
        if(cell.y<1 || cell.y>=world.heightLimit())return;
        if(furniture(id) && World::playerBox(feet,body(),hullHeight()).intersects({cell.v(),cell.v()+Vec{1,1,1}}))return;
        devices.emplace(cell,Device{id,true,normal});
        if(id==SolarPanel || id==CrankGenerator)built=true;
        bag[id]-=FullBlock;refreshGrid();message(std::string("Mounted ")+materialName(id));return;
    }
    if(bag[id]<FullBlock){message(std::string("Gather more ")+materialName(id));return;}
    if(world.sample(level,cell).amount)return;
    const Cell root=parent(cell,subdivisions(level));
    if((legacyRig && built && root==Port)||devices.contains(root))return;
    const double size=1.0/subdivisions(level);
    const Vec lo=cell.v()*size,hi=lo+Vec{size,size,size};
    if(World::playerBox(feet,body(),hullHeight()).intersects({lo,hi}))return;
    if(world.apply({level,cell,id})>0){
        bag[id]-=FullBlock;
        message(std::string("Placed ")+materialName(id)+"  "+meters(size));
    }
}
void Game::toggleSwitch(){
    focus();
    if(!deviceTarget)return;
    auto found=devices.find(*deviceTarget);
    if(found==devices.end())return;
    if(workshopRecipe(found->second.id)){
        auto& machine=found->second;
        const auto i=std::find(materialsList.begin(),materialsList.end(),machine.material);
        selectMachineMaterial(machine,materialsList[(std::size_t(i-materialsList.begin())+1)%materialsList.size()]);
        return;
    }
    if(found->second.id==North){northSound();return;}
    if(found->second.id==Fabricator || found->second.id==PrismPress || found->second.id==HouseBuilder){
        const int id=found->second.id;
        openWorkshop(id==PrismPress?Workshop::Press:id==HouseBuilder?Workshop::House:Workshop::Machine,found->first);
        return;
    }
    if(found->second.id==Switch){
        found->second.closed=!found->second.closed;
        message(found->second.closed?"Switch closed":"Switch open");
        refreshGrid();
    }
}
void Game::refreshGrid(){
    network=legacyRig && built?solveNetwork(devices,Port):Electrical{};
    const auto accumulate=[](auto& total,const auto& branch,double power){
        for(const auto& [cell,ratio]:branch)total[cell]=std::hypot(total[cell],ratio*std::sqrt(power));
    };
    for(const auto& [cell,device]:devices){
        const double power=device.id==CrankGenerator?1:device.id==SolarPanel && !night?2:0;
        if(power==0)continue;
        const auto branch=solveNetwork(devices,cell);
        accumulate(network.lamps,branch.lamps,power);
        accumulate(network.quarries,branch.quarries,power);
        accumulate(network.houses,branch.houses,power);
        network.conductance+=branch.conductance;
        network.connected+=branch.connected;
    }
    dirtyGrid=true;
}

void Game::selectMachineMaterial(Device& machine,int material){
    if(machine.material==material)return;
    machine.material=material;
    machine.energy=machine.cycleEnergy=machine.probeDelay=0;
    message(std::string(machine.id==Quarry?"Quarry target: ":"Resource target: ")+materialName(material));
}

void Game::operateMachine(int key){
    using namespace platform;
    const auto found=devices.find(*machineConsole);
    if(found==devices.end() || key==E){machineConsole.reset();return;}
    auto& machine=found->second;
    if(key==Left || key==Right){
        const auto material=std::find(materialsList.begin(),materialsList.end(),machine.material);
        const int count=int(materialsList.size());
        const int index=(int(material-materialsList.begin())+count+(key==Right?1:-1))%count;
        selectMachineMaterial(machine,materialsList[index]);
    }
    if(workshopRecipe(machine.id) && key==Enter){
        machine.closed=!machine.closed;
        machine.probeDelay=0;
        refreshGrid();
    }
}

double Game::housePower(Cell position) const {
    const auto load=network.houses.find(position);
    if(load==network.houses.end())return 0;
    return generator.rms2*load->second*load->second/houseOhms;
}

bool Game::visibleRecipe(int index) const {
    const auto& plan=recipes[index];
    if(plan.makes==QuartzSight)return false; // permanently built into rifles
    if(plan.power && !built)return false;
    switch(workshop){
        case Workshop::Machine:return machineRecipe(plan.makes);
        case Workshop::Press:return prismRecipe(plan.makes);
        case Workshop::House:return furniture(plan.makes);
        case Workshop::Workbench:
            return !prismRecipe(plan.makes) && !furniture(plan.makes) && !machineRecipe(plan.makes);
    }
    return false;
}

void Game::openWorkshop(Workshop station,std::optional<Cell> position){
    workshop=station;
    workshopCell=position;
    for(int i=0;i<int(recipes.size());i++)if(visibleRecipe(i)){
        recipe=i;crafting=true;return;
    }
    message("Build a generator first");
}

bool Game::ownsWeapon() const {return bag[PulsePistol]>=FullBlock || bag[PulseRifle]>=FullBlock;}

bool Game::nearCrafter() const{
    if(level>=0)return false;
    if(length(CampStation.v()+Vec{0.5,0,0.5}-feet)<6)return true;
    return std::any_of(devices.begin(),devices.end(),[&](const auto& entry){
        return entry.second.id==Fabricator && length(entry.first.v()-feet)<6;
    });
}

void Game::craft(){
    const auto& plan=recipes[recipe];
    if(!visibleRecipe(recipe)){message("Use the station for this recipe");return;}
    if(workshop==Workshop::House){
        if(!workshopCell || !devices.contains(*workshopCell) || length(feet-workshopCell->v())>6 ||
            housePower(*workshopCell)<0.025){message("House Builder needs 25 mW of steady power");return;}
    }else if(prismRecipe(plan.makes)){
        const bool press=level<0 && std::any_of(devices.begin(),devices.end(),[&](const auto& entry){
            return entry.second.id==PrismPress && length(entry.first.v()-feet)<6;
        });
        if(!press){message("Mount a Prism Press within 6 m");return;}
    }else if(machineRecipe(plan.makes)){
        if(!nearCrafter()){message("Use camp's Machine Crafter or mount one within 6 m");return;}
    }else if(!camp()){message("Craft at camp");return;}
    if(plan.makes==North && (bag[North] || std::any_of(devices.begin(),devices.end(),[](const auto& item){return item.second.id==North;}))){message("There is only one North");return;}
    if(plan.makes==Tempering && (!bag[Pick] || bag[IronPick])){
        message("Requires a stone pick without an iron pick");return;
    }
    if((plan.makes==PulsePistol || plan.makes==PulseRifle || plan.makes==PowerPack) && bag[plan.makes]){
        message("You already made this");return;
    }
    if((plan.kiln && !bag[Kiln]) || (plan.power && !built)){message("Prerequisite missing");return;}
    if((plan.makes==Pick || plan.makes==Kiln || plan.makes==IronPick) && bag[plan.makes]){
        message("You already made this");return;
    }
    for(const auto& [id,qty]:plan.cost)if(bag[id]<std::int64_t(qty)*FullBlock){
        message(std::string("Need ")+std::to_string(qty)+" "+materialName(id));return;
    }
    for(const auto& [id,qty]:plan.cost)bag[id]-=std::int64_t(qty)*FullBlock;
    if(plan.makes==Tempering){
        bag[Pick]=0;bag[IronPick]+=FullBlock;
        message("Tempered pick now has infinite durability");
    }else if(plan.makes==Generator){
        built=true;
        bag[CrankGenerator]+=FullBlock;
        selectMaterial(CrankGenerator);
        message("Crank generator ready to place");crafting=false;
    }else{
        bag[plan.makes]+=std::int64_t(plan.count)*FullBlock;
        if(plan.makes==Pick)pickUses=96;
        auto slot=std::find(placement.begin(),placement.end(),plan.makes);
        if(slot!=placement.end())selected=int(slot-placement.begin());
        message(std::string("Built ")+plan.label);
    }
    updateLesson();
}
int Game::awardLoot(int maxCount){
    // Include all player items, but exclude terrain internals and duplicate uniques.
    constexpr auto items=std::to_array<int>({Dirt,Stone,Timber,Clay,CopperOre,IronOre,Coal,Lodestone,Sand,Quartz,
        CampTile,Pick,Kiln,Copper,Iron,Coil,Crank,Magnet,Cable,Lamp,Switch,IronPick,Quarry,North,Fabricator,
        PulsePistol,PulseRounds,PowerPack,Door,Seat,Lantern,Plank,RoofTile,PrismPress,
        PulseCrystal,PulseRifle,ResourceWell,HouseBuilder,MeadowGate,QuartzGate,CrankGenerator,SolarPanel});
    const auto draw=[&]{
        std::uint64_t v=(lootState+=0x9e3779b97f4a7c15ULL);
        v=(v^(v>>30))*0xbf58476d1ce4e5b9ULL;
        v=(v^(v>>27))*0x94d049bb133111ebULL;
        return v^(v>>31);
    };
    for(std::size_t attempt=0;attempt<items.size();attempt++){
        const int item=items[draw()%items.size()];
        if(item==North && std::any_of(devices.begin(),devices.end(),[](const auto& d){return d.second.id==North;}))continue;
        if((item==MeadowGate && gates[0]) || (item==QuartzGate && gates[1]))continue;
        const bool unique=item==North || item==MeadowGate || item==QuartzGate || item==PulsePistol ||
            item==PulseRifle || item==Pick || item==IronPick || item==Kiln || item==PowerPack;
        if(unique && bag[item])continue;
        const auto count=unique?1:1+draw()%maxCount;
        bag[item]=std::min(std::int64_t(999999)*FullBlock,bag[item]+std::int64_t(count)*FullBlock);
        if(item==Pick)pickUses=96;
        message("Found "+std::to_string(count)+" "+materialName(item));
        return item;
    }
    bag[Stone]=std::min(std::int64_t(999999)*FullBlock,bag[Stone]+FullBlock);
    message("Found 1 Stone");
    return Stone;
}

void Game::rollLoot(){
    if(rollCooldown>0)return;
    rollCooldown=1.4;
    awardLoot(8);
}

void Game::key(int key) {
    using namespace platform;
    if(bindingCapture){bindInput(key);return;}
    if(settingsPage){
        if(key==Escape){settingsPage=settingsPage==1?0:1;return;}
        if(settingsPage==1){
            if(key==Enter || key==T)settingsPage=2;
            if(key==G)settingsPage=3;
            if(key==Left)renderer.renderScale(std::max(25,renderer.renderScale()-5));
            if(key==Right)renderer.renderScale(std::min(200,renderer.renderScale()+5));
            if(key==Up)sensitivity=std::min(2.6,sensitivity+0.1);
            if(key==Down)sensitivity=std::max(0.4,sensitivity-0.1);
        }else if(settingsPage==3){
            if(key==Up)graphicsRow=std::max(0,graphicsRow-1);
            if(key==Down)graphicsRow=std::min(GraphicsOptions::Count-1,graphicsRow+1);
            if(key==Left)renderer.graphics().adjust(graphicsRow,-1);
            if(key==Right || key==Enter)renderer.graphics().adjust(graphicsRow,1);
            if(key==Backspace)renderer.graphics()=GraphicsOptions{};
            if(key>=One && key<=Four)renderer.graphics().preset(key-One);
        }else{
            if(key==Up)bindingRow=std::max(0,bindingRow-1);
            if(key==Down)bindingRow=std::min(int(input::actions.size())-1,bindingRow+1);
            if(key==Enter && input::actions[bindingRow].first!=G)bindingCapture=true;
            if(key==Backspace){bindings->reset();if(!bindings->save())message("Could not save controls");}
        }
        return;
    }
    if(key==Escape) {
        if(machineConsole){machineConsole.reset();}
        else if(inventory){inventory=false;}
        else if(crafting||materials||electrical||map){crafting=materials=electrical=map=false;}
        else paused=!paused;
        return;
    }
    if(paused){
        if(key==T){settingsPage=1;return;}
        if(key==Q || (key==W && !worldTitle.empty())){
            if(save()){
                saveFile.clear();
                done=key==Q;
                pickWorld=key==W;
            }else message("Could not save this world. Your session is still open.");
            return;
        }
        if(key==Enter)paused=false;
        if(key==Up)sensitivity=std::min(2.6,sensitivity+0.1);
        if(key==Down)sensitivity=std::max(0.4,sensitivity-0.1);
        if(key==R){surfaceFortress=false;level=family=-1;feet={0.5,29,-7.1};yaw=Pi;pitch=-0.16;vertical.reset();zoom=goalZoom=0;precision=0;paused=false;world.stream(floorCell(feet),2);}
        return;
    }
    if(inventory){if(key==Tab)inventory=false;return;}
    if(machineConsole){operateMachine(key);return;}
    if(crafting){
        if(key==C)crafting=false;
        if(key==Up || key==W || key==Down || key==S){
            const int direction=(key==Up || key==W)?-1:1;
            for(int next=recipe+direction;next>=0 && next<int(recipes.size());next+=direction){
                if(visibleRecipe(next)){recipe=next;break;}
            }
        }
        if(key==Enter || key==Space)craft();
        return;
    }
    if(materials){
        const int end=int(buildPalette.size())-1;
        if(key==H){materials=false;return;}
        if(key==Left || key==A)picker=std::max(0,picker-1);
        if(key==Right || key==D)picker=std::min(end,picker+1);
        if(key==Up || key==W)picker=std::max(0,picker-2);
        if(key==Down || key==S)picker=std::min(end,picker+2);
        if(key==Enter){selectMaterial(buildPalette[picker]);materials=false;}
        return;
    }
    if(electrical){
        if(key==R || key==Enter)electrical=false;
        if(key==Up || key==W)generator.rpm=std::min(240.0,generator.rpm+10);
        if(key==Down || key==S)generator.rpm=std::max(0.0,generator.rpm-10);
        if(key==Left)generator.turns=std::max(1,generator.turns-4);
        if(key==Right)generator.turns=std::min(80,generator.turns+4);
        if(key==I)generator.test=!generator.test;
        if(key==M)generator.field=generator.field==0.08?0.32:generator.field==0.32?0:0.08;
        return;
    }
    if(map){if(key==M)map=false;return;}
    if(key==Plus)wheel(1);
    if(key==Minus)wheel(-1);
    if(key==F3)debug=!debug;
    if(key==V){flying=!flying;vertical.reset();message(flying?"Flight on  |  "+keyName(Space)+" up, "+keyName(Q)+" down":"Flight off");}
    if(key>=One && key<=Seven)selected=key-One;
    if(key==LeftBracket || key==RightBracket){
        const int count=int(placement.size()),step=key==RightBracket?1:-1;
        for(int i=1;i<=count;i++){
            const int slot=(selected+count+step*i)%count;
            if(bag[placement[slot]]>0){selectMaterial(placement[slot]);break;}
        }
    }
    if(key==H){
        if(deviceTarget && devices.contains(*deviceTarget) && devices.at(*deviceTarget).id==Quarry &&
           !mounted(placement[selected])){
            auto& machine=devices.at(*deviceTarget);
            selectMachineMaterial(machine,placement[selected]);
            return;
        }
        const auto found=std::find(buildPalette.begin(),buildPalette.end(),placement[selected]);
        picker=found==buildPalette.end()?0:int(found-buildPalette.begin());
        materials=true;
    }
    if(key==C){
        if(level>=0)message("Return to camp to use the workbench");
        else if(camp())openWorkshop(Workshop::Workbench);
        else message("Return to camp to use the workbench");
    }
    if(key==X){
        if(!ownsWeapon()){message("Craft a weapon at the Prism Press");return;}
        if(!armed)weapon=bag[PulsePistol]?PulsePistol:PulseRifle;
        else if(weapon==PulsePistol && bag[PulseRifle])weapon=PulseRifle;
        else {armed=false;sightOn=false;aiming=false;message("Weapon holstered");return;}
        armed=true;sightOn=weapon==PulseRifle;message(std::string(materialName(weapon))+" equipped");return;
    }
    if(key==Z){
        message(armed && weapon==PulseRifle?"Rifle optic is permanently installed":"Equip a rifle to use its integrated optic");
        return;
    }
    if(key==B && level>=0){ascend();return;}
    if(key==E){
        const int oldFamily=family,oldLevel=level;
        const Vec point=deviceTarget?deviceTarget->v()+Vec{.5,.5,.5}:camera();
        if(interact(true)){if(oldFamily==family && oldLevel==level)noticeWitness(point);return;}
    }
    if(key==F)flashlight=!flashlight;
    if(key==I){
        const auto before=level<0?world.revision():layer(family,level).blocks().revision();
        const Vec point=target?camera()+viewDirection(yaw,pitch)*target->distance:camera();
        collect(true);
        if(before!=(level<0?world.revision():layer(family,level).blocks().revision()))noticeWitness(point);
    }
    if(key==Tab)inventory=true;
    if(key==G)rollLoot();
    if(key==R){
        if(deviceTarget==CampStation && !devices.contains(CampStation))openWorkshop(Workshop::Machine,CampStation);
        else if(deviceTarget)toggleSwitch();
        else if(atGenerator())electrical=true;
    }
    if(key==M && level<0)map=true;
    if(key==P){
        if(modernWorld){solarClock=night?solarcycle::CycleSeconds*.5:0;night=solarcycle::at(solarClock).night;}
        else night=!night;
        refreshGrid();
    }
}
bool Game::interact(bool directKey){
    focus();
    if(useGoblinChest())return true;
    if(level>=0 && target && furniture(target->id)){
        const int next=alternateFurniture(target->id);
        if(next!=target->id){
            const auto lower=target->cell.v();
            if(next==Door && World::playerBox(innerFeet,innerSize(),hullHeight()).intersects({lower,lower+Vec{1,1,1}})){
                message("Step clear of the door");return true;
            }
            layer(family,level).set(target->cell,next);
            focus();
            return true;
        }
    }
    if(level<0){
        focus();
        if(deviceTarget){
            if(*deviceTarget==CraftTable){openWorkshop(Workshop::Workbench,CraftTable);return true;}
            if(*deviceTarget==CampStation && !devices.contains(CampStation)){openWorkshop(Workshop::Machine,CampStation);return true;}
            const auto machine=devices.find(*deviceTarget);
            if(machine!=devices.end()){
                if(machine->second.id==CrankGenerator){electrical=true;return true;}
                if(workshopRecipe(machine->second.id)){machineConsole=deviceTarget;return true;}
                if(machine->second.id==Fabricator || machine->second.id==PrismPress || machine->second.id==HouseBuilder || machine->second.id==North || machine->second.id==Switch){toggleSwitch();return true;}
                if(alternateFurniture(machine->second.id)!=machine->second.id){
                    const auto shape=expedition::furnitureVolume(machine->second.id,machine->first.v(),machine->second.closed);
                    if(World::playerBox(feet,body(),hullHeight()).intersects(shape)){
                        message("Step clear of the furniture");return true;
                    }
                    machine->second.closed=!machine->second.closed;
                    dirtyGrid=true;return true;
                }
            }
        }
    }
    if(level==expedition::Village){
        const auto eye=camera(),dir=viewDirection(yaw,pitch);
        auto& residents=wildlife(family,level);
        auto& folks=residents.elves;
        constexpr std::array lines={
            "Aurel: Welcome, Delbert. There's a bed of quartz past the last lantern.",
            "Mira: We build around the trees. Their roots hold the whole village up.",
            "Lethan: Beyond the banners, stay on the lit trail. Crawlers hide among the roots.",
            "Sable: Cedar chairs are for guests. You're welcome by the lantern."};
        double best=4;
        int choice=-1;
        for(int i=0;i<int(folks.size());i++){
            if(residents.missingElves[i])continue;
            const auto offset=folks[i].feet+Vec{0,1.38,0}-eye;
            const double d=length(offset);
            if(d<best && dot(unit(offset),dir)>0.94){best=d;choice=i;}
        }
        if(choice>=0){
            creatureVoice(choice,false,0,.75,true);noticeWitness(folks[choice].feet+Vec{0,1,0});
            message(lines[choice]);return true;
        }
    }
    if((directKey || gateTarget || (level>=0 && length(innerFeet-micro::Descent)<2.0)) && descend(true))return true;
    if(deviceTarget==Port){electrical=true;return true;}
    return false;
}
void Game::selectMaterial(int id) {
    const auto it=std::find(placement.begin(),placement.end(),id);
    if(it==placement.end())return;
    selected=int(it-placement.begin());
    message(std::string("Selected ")+materialName(id));
}

void Game::button(int button,bool modifier) {
    if(!wantsCapture())return;
    if(button==1){
        if(armed)fire();
        else{
            const auto before=level<0?world.revision():layer(family,level).blocks().revision();
            const Vec point=target?camera()+viewDirection(yaw,pitch)*target->distance:camera();
            collect();
            if(before!=(level<0?world.revision():layer(family,level).blocks().revision()))noticeWitness(point);
        }
    }
    if(button==2){
        focus();
        if(target)selectMaterial(furniture(target->id)?furnitureBase(target->id):harvestMaterial(target->id));
    }
    if(button==3 && (!armed || modifier)){
        const auto before=level<0?world.revision():layer(family,level).blocks().revision();
        const int oldFamily=family,oldLevel=level;
        const Vec point=target?camera()+viewDirection(yaw,pitch)*target->distance:camera();
        if(!interact(false))place(modifier && !armed);
        if(oldFamily==family && oldLevel==level && before!=(level<0?world.revision():layer(family,level).blocks().revision()))noticeWitness(point);
    }
}

void Game::updateLesson() {
    auto has=[&](int id,int qty=1){return bag[id]>=std::int64_t(qty)*FullBlock;};
    if(built){lesson=7;return;}
    if(has(Pick))lesson=std::max(lesson,2);
    if((has(CopperOre,4)&&has(IronOre,4)&&has(Clay,4)&&has(Lodestone,2))||has(Kiln))lesson=std::max(lesson,3);
    if(has(Kiln)&&(has(Coal,4)||has(Copper)||has(Iron)||has(Coil)||has(Crank)))lesson=std::max(lesson,4);
    if(has(Coil)&&has(Crank)&&has(Magnet))lesson=std::max(lesson,5);
    if(lesson==0 && has(Timber,2) && has(Stone,3))lesson=1;
}
void Game::frameDuration(double elapsed){
    if(elapsed>0 && elapsed<0.5)
        frameSeconds=std::lerp(frameSeconds>0?frameSeconds:elapsed,elapsed,1-std::exp(-elapsed*4));
}

void Game::update(double dt,const Motion& controls,double elapsed) {
    if(!paused)rollCooldown=std::max(0.0,rollCooldown-dt);
    statusTime=std::max(0.0,statusTime-dt);
    if(portalZoom && (!ownsWeapon() || (level<0?entrance()<0:length(innerFeet-micro::Descent)>3.1)))
        portalZoom=false;
    if(!wantsCapture())smoothInput={};
    if(!paused){
        if(wantsCapture()){
            const int steps=std::max(1,int(std::ceil(dt*60)));
            for(int i=0;i<steps;i++)movement(dt/steps,controls);
        }
        if(level<0) {
            const Cell here=parent(floorCell(feet),32);
            static_cast<void>(visited.emplace(here,true));
            world.streamInteractive(floorCell(feet),3);
            if(wantsCapture())descend(false);
            focus();
            if(!crawled && feet.x>=72 && feet.x<=82 && feet.y<20 && feet.z>=20 && feet.z<=29){
                crawled=true;message("Survey logged: Quartz chamber");
            }
        }else if(level>=0)focus();
        aiming=armed && controls.aim && !controls.crouch && wantsCapture();
        updateExpedition(dt);
        updateGoblin(dt);
        updateNorth(dt);
        if(wantsCapture() && combatEnabled)updateForestLife(dt);
        updateWitness(dt);
        workWells(std::clamp(elapsed>=0?elapsed:dt,0.0,5.0));
        const double time=dt*timeRate();
        playTime+=time;
        if(modernWorld){
            solarClock=solarcycle::wrap(solarClock+std::clamp(elapsed>=0?elapsed:dt,0.0,2.0));
            const bool nextNight=solarcycle::at(solarClock).night;
            if(nextNight!=night){night=nextNight;refreshGrid();}
        }
        if(built){
            const int steps=std::max(1,int(std::ceil(time*180)));
            const double stepTime=time/steps;
            double sourceEnergy=0;
            const double inductionLoad=bag[PowerPack] && camp() && battery<expedition::BatteryCapacity?0.055:0;
            for(int i=0;i<steps;i++){
                generator.advance(stepTime,network.conductance+inductionLoad);
                sourceEnergy+=generator.voltage*generator.voltage*stepTime;
            }
            if(level<0){
                workQuarries(time,sourceEnergy);
            }
            if(!wired)for(const auto& [position,ratio]:network.lamps)if(generator.rms2*ratio*ratio/lampOhms>=0.25){
                wired=true;message("First field light");break;
            }
        }
    }
    updateHorrorAudio(dt);
    updateLesson();
    if((fileTimer+=dt)>7){fileTimer=0;save();}
}

std::optional<Cell> Game::findDeposit(Cell machine,int material,int range) const {
    const auto player=World::playerBox(feet,body(),hullHeight());
    const auto footing=Box{player.lo-Vec{0,0.1,0},player.hi};
    for(int radius=1;radius<=range;radius++)
        for(int depth=1;depth<=radius;depth++)
            for(int dz=-radius;dz<=radius;dz++)for(int dx=-radius;dx<=radius;dx++){
                if(std::max({std::abs(dx),std::abs(dz),std::abs(depth)})!=radius)continue;
                const Cell cell=machine+Cell{dx,-depth,dz};
                if(cell.y<=0 || devices.contains(cell+Cell{0,1,0}) ||
                    cell+Cell{0,1,0}==Port || cell+Cell{0,1,0}==CampStation)continue;
                if(footing.intersects({cell.v(),cell.v()+Vec{1,1,1}}))continue;
                const auto sample=world.sample(0,cell);
                if(!sample.solid() || harvestMaterial(sample.id)!=material || protectedMaterial(sample.id))continue;
                bool supports=false;
                for(const auto side:Sides){
                    auto d=devices.find(cell+side);
                    supports|=d!=devices.end() && d->second.normal==side;
                }
                if(supports)continue;
                auto contents=world.composition(0,cell);
                MatterVolume matching=0;
                for(int id=1;id<int(contents.size());++id)if(harvestMaterial(id)==material)matching+=contents[id];
                if(matching==FullVolume && !contents[CampTile] && !contents[Bedrock] && !contents[Water])return cell;
            }
    return {};
}

void Game::workQuarries(double dt,double sourceEnergy){
    if(sourceEnergy<=0 || generator.field==0)return;
    for(const auto& [cell,ratio]:network.quarries){
        auto& device=devices.at(cell);
        if(length(cell.v()-feet)>machineRange || ratio<=0)continue;
        if(device.cycleEnergy<=0){
            if(generator.rms2<0.01)continue;
            device.cycleEnergy=4*std::ceil(generator.rms2/quarryOhms);
        }
        device.energy=std::min(device.cycleEnergy,device.energy+sourceEnergy*ratio*ratio/quarryOhms);
        if(device.energy<device.cycleEnergy)continue;
        device.probeDelay=std::max(0.0,device.probeDelay-dt);
        if(device.probeDelay>0)continue;
        if(auto deposit=findDeposit(cell,device.material,quarryRadius);deposit && world.apply({0,*deposit,Air})<0){
            device.energy=device.cycleEnergy=0;
            bag[device.material]+=FullBlock;
            message(std::string("Quarry +1 ")+materialName(device.material));
        }else device.probeDelay=1.0;
    }
}

void Game::workWells(double dt){
    const auto maxBalance=std::int64_t(FullBlock)*999999;
    for(auto& [cell,well]:devices){
        if(well.id!=ResourceWell || !well.closed)continue;
        well.energy+=dt;
        const auto blocks=static_cast<std::int64_t>(well.energy+1e-9);
        if(!blocks)continue;
        well.energy=std::max(0.0,well.energy-blocks);
        if(bag[well.material]<maxBalance)
            bag[well.material]=std::min(maxBalance,bag[well.material]+blocks*FullBlock);
    }
}

const std::vector<Vec>& Game::cachedTrail(Cell tile){
    static const std::vector<Vec> empty;
    if(!world.seed || !world.sculptedTerrain || tile.x< -64 || tile.x>=64 || tile.z< -64 || tile.z>=64)return empty;
    tile.y=0;
    const auto revision=world.detailVersion({tile*32,tile*32+Cell{32,world.heightLimit(),32}},0);
    if(auto found=trailCache.find(tile);found!=trailCache.end() &&
        found->second.revision==revision && found->second.seed==*world.seed)return found->second.posts;
    world.require(tile.x,tile.z);
    auto& cached=trailCache[tile];cached.revision=revision;cached.seed=*world.seed;
    cached.posts=natural::groundedTrail(world,tile.x,tile.z);
    return cached.posts;
}

void Game::terrainMesh(Chunks& mesh,Cell chunk,int depth,bool proxyOnly) {
    Geometry near,far,exact,contours,trees,leafSprigs,trailPosts,trailFlames;
    const Cell origin{chunk.x*32,0,chunk.z*32};
    const bool forestMesh=modernWorld && world.seed && world.sculptedTerrain;
    // Dual cells at the tile border need one sample from the adjacent tile.
    if(forestMesh)for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx)
        world.require(chunk.x+dx,chunk.z+dz);
    const auto treeSample=[&](Cell root){
        // Most stencil cells contain terrain or air. Reject them using the
        // natural-tree bit before the considerably more costly matter query.
        return world.uneditedTreeMaterial(root);
    };
    const auto treeRevision=world.treeVersion(chunk);
    const bool rebuildTrees=forestMesh && (!mesh.treeReady || mesh.treeVersion!=treeRevision ||
                                          (!proxyOnly && !mesh.treeDetail));
    if(rebuildTrees)arbor::mesh(trees,leafSprigs,{origin,origin+Cell{32,world.heightLimit(),32}},treeSample,!proxyOnly);
    // Repeated height, face and proxy queries share the same root-cell
    // samples. This cache is local to one rebuild, never stale after editing.
    constexpr int width=34;
    const int height=world.heightLimit()+2;
    std::vector<Sample> sampled(width*height*width,Sample{Air,-1});
    const auto coarse=[&](Cell root){
        const Cell p=root-origin+Cell{1,1,1};
        if(p.x<0 || p.x>=width || p.z<0 || p.z>=width || p.y<0 || p.y>=height)
            return world.sample(0,root);
        auto& v=sampled[(p.z*height+p.y)*width+p.x];
        if(v.amount<0)v=world.sample(0,root);
        return v;
    };
    // Shared corner samples, calculated once per chunk. A material-only
    // overlay uses these on the actual roof triangles, so no z-fighting or
    // floating path surfaces and no extra physics cells are introduced.
    std::array<float,33*33> pathEdge{};
    pathEdge.fill(-2.f);
    if(forestMesh)for(int z=0;z<=32;++z)for(int x=0;x<=32;++x)
        pathEdge[z*33+x]=float(natural::trailEdge(origin.x+x,origin.z+z,*world.seed));
    const auto nearPath=[&](int x,int z){
        return std::max({pathEdge[z*33+x],pathEdge[z*33+x+1],pathEdge[(z+1)*33+x],pathEdge[(z+1)*33+x+1]})>-.85f;
    };
    const auto shadePath=[&](Geometry& mesh,std::size_t first,int x,int z,int material){
        if(!forestMesh || (material!=Grass && material!=Dirt) || !nearPath(x,z))return;
        const float a=pathEdge[z*33+x],b=pathEdge[z*33+x+1],c=pathEdge[(z+1)*33+x+1],d=pathEdge[(z+1)*33+x];
        for(std::size_t i=first;i<mesh.points.size();++i){
            auto& v=mesh.points[i];
            const float u=std::clamp(v.x-x,0.f,1.f),w=std::clamp(v.z-z,0.f,1.f);
            // Match the roof diagonal, including triangles clipped by a voxel.
            v.u=u>=w?a+(b-a)*u+(c-b)*w:a+(c-d)*u+(d-a)*w;
            v.id=float(TrailSurfaceTexture);
        }
    };
    std::optional<TerrainFaces> faces;
    if(!proxyOnly)faces.emplace(Bounds{{0,0,0},{32,world.heightLimit(),32}});
    std::array<int,32*32> elevations{};
    auto terrainSample=[&](Cell root){
        const auto block=coarse(root);
        // A distant proxy may simplify a natural cube. Isolated fine placements
        // stay out of its height map so a tiny chip cannot become a distant tower.
        const bool tree=forestMesh && arbor::treeMaterial(block.id) && block.amount==FullVolume &&
            world.naturalTree(root) && !world.altered(root);
        return block.amount && !tree && (!world.editedChildren(root) || block.amount>=FullVolume/2);
    };
    for(int z=0;z<32;z++) for(int x=0;x<32;x++) {
        int top=world.heightLimit()-1;
        while(top>0 && !coarse(origin+Cell{x,top,z}).amount)top--;
        int silhouette=top;
        while(silhouette>0 && !terrainSample(origin+Cell{x,silhouette,z}))silhouette--;
        elevations[z*32+x]=silhouette;
        if(proxyOnly)continue;
        const Cell column=origin+Cell{x,0,z};
        const auto* profile=world.roof(column);
        int firstY=std::max(1,std::min(top-10,depth-11));
        // Looking down from the canopy must not drop the ground below it.
        if(forestMesh && profile)firstY=std::min(firstY,std::max(1,int(std::floor(profile->low()))-1));
        for(int y=firstY;y<=top;y++){
            const Cell local{x,y,z},root=origin+local;
            const auto block=coarse(root);
            if(!block.amount)continue;
            const float tone=0.91f+(CellHash{}(root)&31)*0.0045f;
            const Color tint{tone,tone,tone};
            if(profile && profile->covers(y) && world.altered(root)){
                terrain::editedRoot(exact,world,root,origin.v(),tint,!world.sculptedTerrain);
                continue;
            }
            // A tree root crossing the ground roof needs both a turf plane
            // and its continuous wood mesh. Never clip the tree shell to a
            // terrain cap or paint an entire ground column brown.
            const bool living=forestMesh && arbor::treeMaterial(block.id) && treeSample(root)!=Air;
            if(profile && block.id!=Water && profile->covers(y)){
                const int roofMaterial=living?(profile->high()<24.5?Sand:
                    natural::trailSurface(root.x+.5,root.z+.5,*world.seed)?Dirt:Grass):block.id;
                const auto roofStart=contours.points.size();
                if(profile->low()>=y && profile->high()<=y+1)
                    terrain::roofTop(contours,*profile,{double(x),0,double(z)},roofMaterial,tint);
                else terrain::roofFragment(contours,*profile,{root,0,roofMaterial},origin.v(),tint);
                shadePath(contours,roofStart,x,z,roofMaterial);
                for(int side:{0,1,4,5}){
                    const Cell next=root+Sides[side];
                    if(world.altered(next)){
                        terrain::immediateBoundary(contours,world,{root,0,block.id},side,origin.v(),tint);
                        continue;
                    }
                    const auto* adjacent=world.roof(next);
                    if(!adjacent && coarse(next).solid())continue;
                    terrain::roofWall(contours,*profile,adjacent,root,origin.v(),side,roofMaterial,tint);
                }
                continue;
            }
            if(living)continue;
            if(world.editedChildren(root)){
                terrain::editedRoot(exact,world,root,origin.v(),tint,!world.sculptedTerrain);
                continue;
            }
            const auto hash=CellHash{}(root);
            if(const auto piece=world.shapedPiece({root,0,block.id})){
                terrain::stone(exact,local.v(),1.0,block.id,tint,*piece);
                continue;
            }
            const bool shape=terrain::mineral(block.id)||terrain::organic(block.id);
            const bool bevel=shape && (!modernWorld || block.id!=Grass) && terrain::bevelCell(hash);
            const unsigned air=(bevel || (!modernWorld && terrain::mineral(block.id)))?
                terrain::exposed([&](Cell d){return coarse(root+d).amount;}):0;
            const bool onFloor=terrain::mineral(block.id) && terrain::onFloor(air) &&
                coarse(root+Sides[2]).amount==FullVolume;
            if(!modernWorld && (air==63 || onFloor)){
                auto& layer=terrain::mineral(block.id)?exact:near;
                terrain::stone(layer,local.v(),1.0,block.id,tint,hash,onFloor);
                continue;
            }
            for(int side=0;side<6;side++){
                const Cell neighbor=root+Sides[side];
                const int material=block.id==Grass && side==2?Dirt:block.id;
                if(world.editedChildren(neighbor)){
                    terrain::immediateBoundary(near,world,{root,0,material},side,origin.v(),tint);
                }else{
                    const auto adjacent=coarse(neighbor);
                    if(adjacent.amount && (adjacent.id!=Water || block.id==Water)){
                        if(world.leafRoof({neighbor,0,adjacent.id}))
                            terrain::immediateBoundary(near,world,{root,0,material},side,origin.v(),tint);
                        continue;
                    }
                    if(bevel && terrain::beveled(air,side))
                        terrain::face(near,local.v(),1.0,side,material,tint,hash,air);
                    else faces->add(local,side,material);
                }
            }
        }
    }
    if(faces)faces->draw(near,origin,1.0,0.91f,0.14f);
    const auto proxyHeight=[&](int x,int z){
        if(x>=0 && x<32 && z>=0 && z<32)return elevations[z*32+x]+1;
        int top=world.heightLimit()-1;
        while(top>0 && !terrainSample(origin+Cell{x,top,z}))top--;
        return top+1;
    };
    // Share the four sample points at the edge of each distant tile.
    // Diagonal facets roll across the landscape without the old tall proxy steps.
    const auto proxyCorner=[&](int x,int z){
        const Cell column=origin+Cell{x,0,z};
        for(const Cell offset:{Cell{0,0,0},Cell{-1,0,0},Cell{0,0,-1},Cell{-1,0,-1}}){
            const auto* roof=world.roof(column+offset);
            if(!roof)continue;
            const int top=int(std::ceil(roof->high()))-1;
            const Cell cap=column+offset+Cell{0,top,0};
            if(!world.altered(cap) || coarse(cap).amount>=FullVolume/2)
                return roof->height(-offset.x,-offset.z);
        }
        return 0.25*(proxyHeight(x-1,z-1)+proxyHeight(x,z-1)+proxyHeight(x-1,z)+proxyHeight(x,z));
    };
    std::array<double,81> surface{};
    for(int z=0;z<=8;z++)for(int x=0;x<=8;x++)surface[z*9+x]=proxyCorner(x*4,z*4);
    for(int z=0;z<8;z++)for(int x=0;x<8;x++){
        const int y=proxyHeight(x*4+2,z*4+2);
        const Cell column=origin+Cell{x*4+2,0,z*4+2};
        int id=coarse(column+Cell{0,y-1,0}).id;
        if(forestMesh){
            // Proxy geometry follows the ground roof, not the canopy. Its
            // material must come from that same ground surface.
            if(const auto* roof=world.roof(column)){
                const int cap=int(std::ceil(roof->height(.5,.5)))-1;
                id=coarse(column+Cell{0,cap,0}).id;
                if(arbor::treeMaterial(id))id=roof->high()<24.5?Sand:Grass;
                if(roof->high()<24 && y>=24)id=Water;
            }
        }
        const auto point=[&](int xx,int zz){return Vec{xx*4.0,surface[zz*9+xx],zz*4.0};};
        const auto a=point(x,z),b=point(x+1,z),c=point(x+1,z+1),d=point(x,z+1);
        const Color tint{0.93f,0.96f,0.95f};
        const int material=id?id:Stone;
        // Keep the narrow trail visible even on the distant 4m terrain mesh.
        // Use unit material samples but interpolate inside the original proxy
        // triangles: neighboring coarse quads share precisely the same edges.
        bool trailPatch=false;
        if(forestMesh)for(int zz=0;zz<4 && !trailPatch;++zz)for(int xx=0;xx<4;++xx)
            if(nearPath(x*4+xx,z*4+zz)){trailPatch=true;break;}
        if(trailPatch){
            const auto pointFine=[&](int xx,int zz){
                const double u=xx*.25,v=zz*.25;
                double h=std::lerp(std::lerp(a.y,b.y,u),std::lerp(d.y,c.y,u),v);
                // Boundary tiles already use exact unit roof samples along
                // their outer edges. Preserve those same seam vertices.
                if((x==0 && xx==0)||(x==7 && xx==4)||(z==0 && zz==0)||(z==7 && zz==4))
                    h=proxyCorner(x*4+xx,z*4+zz);
                return Vec{x*4.0+xx,h,z*4.0+zz};
            };
            for(int zz=0;zz<4;++zz)for(int xx=0;xx<4;++xx){
                const Cell col=origin+Cell{x*4+xx,0,z*4+zz};
                int mat=material;bool intact=false;
                if(const auto* roof=world.roof(col)){
                    const Cell cap=col+Cell{0,int(std::ceil(roof->height(.5,.5)))-1,0};
                    if(!world.altered(cap)){
                        intact=true;mat=coarse(cap).id;
                        if(arbor::treeMaterial(mat))mat=roof->high()<24.5?Sand:
                            natural::trailSurface(col.x+.5,col.z+.5,*world.seed)?Dirt:Grass;
                        if(!mat)mat=material;
                    }
                }
                const auto pathStart=far.points.size();
                far.quad(pointFine(xx,zz),pointFine(xx,zz+1),pointFine(xx+1,zz+1),pointFine(xx+1,zz),mat,tint,1.0f);
                if(intact)shadePath(far,pathStart,x*4+xx,z*4+zz,mat);
            }
        }else if(world.sculptedTerrain && (x==0 || x==7 || z==0 || z==7)){
            const int xx=x*4,zz=z*4;
            const Vec center{xx+2.0,proxyCorner(xx+2,zz+2),zz+2.0};
            const auto edge=[&](Vec first,Vec last,bool outside){
                if(!outside){far.triangle(first,last,center,material,tint,0.25f);return;}
                Vec previous=first;
                for(int step=1;step<4;step++){
                    Vec current=first+(last-first)*(step*0.25);
                    current.y=proxyCorner(int(current.x),int(current.z));
                    far.triangle(previous,current,center,material,tint,0.25f);
                    previous=current;
                }
                far.triangle(previous,last,center,material,tint,0.25f);
            };
            edge(a,d,x==0);edge(d,c,z==7);edge(c,b,x==7);edge(b,a,z==0);
        }else if(std::abs(a.y-c.y)<std::abs(b.y-d.y)){
            far.triangle(a,d,c,material,tint,0.25f);
            far.triangle(a,c,b,material,tint,0.25f);
        }else{
            far.triangle(a,d,b,material,tint,0.25f);
            far.triangle(b,d,c,material,tint,0.25f);
        }
    }
    mesh.terrain.uploadTerrain(near);mesh.proxy.uploadTerrain(far);mesh.exact.uploadTerrain(exact);mesh.contours.uploadTerrain(contours);
    // Preserve cross-tile normals computed from the identical dual stencil.
    if(rebuildTrees){
        mesh.trees.upload(trees);mesh.trees.terrainSurface=true;
        mesh.leafSprigs.upload(leafSprigs);mesh.leafSprigs.terrainSurface=true;mesh.leafSprigs.twoSidedLeaf=true;
        mesh.treeVersion=treeRevision;mesh.treeReady=true;mesh.treeDetail=!proxyOnly;
    }
    if(forestMesh)for(Vec p:cachedTrail(chunk))
        trailFixture(trailPosts,trailFlames,p-origin.v());
    mesh.trailPosts.upload(trailPosts);mesh.trailFlames.upload(trailFlames);
    mesh.version=world.meshVersion(chunk);mesh.depth=depth;mesh.proxyOnly=proxyOnly;
}

void Game::finerMesh(const World& matter,DetailMesh& mesh,Bounds area,int level,std::uint64_t revision){
    Geometry geometry;
    const double size=1.0/subdivisions(level);
    const int division=subdivisions(level);
    const Cell outer=area.hi-area.lo+Cell{2,2,2};
    const Cell padding=area.lo-Cell{1,1,1};
    TerrainFaces faces({{},area.hi-area.lo});
    struct RenderCell {std::uint8_t material;bool exact;};
    std::vector<RenderCell> samples(std::size_t(outer.x)*outer.y*outer.z);
    const auto at=[&](Cell cell)->RenderCell& {
        const Cell p=cell-padding;
        return samples[(std::size_t(p.z)*outer.y+p.y)*outer.x+p.x];
    };
    const Cell firstRoot=parent(padding,division);
    const Cell lastRoot=parent(area.hi,division);
    const int quarterShift=2*(level-1);
    constexpr auto fullMask=~std::uint64_t{};
    for(int rz=firstRoot.z;rz<=lastRoot.z;rz++)for(int ry=firstRoot.y;ry<=lastRoot.y;ry++)
        for(int rx=firstRoot.x;rx<=lastRoot.x;rx++){
            const Cell root{rx,ry,rz};
            const Cell corner=root*division;
            const auto block=matter.pattern(root);
            if(!block.material)continue;
            const bool uniform=block.mask==fullMask;
            const auto* profile=matter.roof(root);
            const bool living=matter.naturalTree(root) && !matter.altered(root);
            const bool fixedPiece=matter.sculptedTerrain?!living && matter.shapedPiece({root,0,block.material}).has_value():
                this->level!=micro::Count-1 && terrain::mineral(block.material) &&
                terrain::piece(matter,{root,0,block.material});
            const bool persistent=block.refined || (profile && profile->covers(root.y) && !matter.treeThroughRoof(root)) || fixedPiece;
            for(int z=std::max(padding.z,corner.z);z<=std::min(area.hi.z,corner.z+division-1);z++)
                for(int y=std::max(padding.y,corner.y);y<=std::min(area.hi.y,corner.y+division-1);y++)
                    for(int x=std::max(padding.x,corner.x);x<=std::min(area.hi.x,corner.x+division-1);x++){
                        if(!uniform){
                            const int part=((x-corner.x)>>quarterShift)+4*(((y-corner.y)>>quarterShift)+
                                4*((z-corner.z)>>quarterShift));
                            if(((block.mask>>part)&1)==0)continue;
                        }
                        at({x,y,z})={std::uint8_t(block.material),persistent};
                    }
        }
    for(int z=area.lo.z;z<area.hi.z;z++)
        for(int y=area.lo.y;y<area.hi.y;y++)
            for(int x=area.lo.x;x<area.hi.x;x++){
                const Cell atWorld{x,y,z};
                const auto tile=at(atWorld);
                const int material=tile.material;
                if(tile.exact || !material)continue;
                const Cell local=atWorld-area.lo;
                const Vec point=local.v()*size;
                const std::uint32_t key=CellHash{}(atWorld);
                const float variation=0.78f+(key&63)*0.0056f;
                const float depthTint=1-0.027f*level;
                const Color tint{variation,variation*depthTint,variation};
                const bool shape=!matter.sculptedTerrain && (terrain::mineral(material)||terrain::organic(material));
                const bool bevel=shape && terrain::bevelCell(key);
                const unsigned air=(bevel || (shape && terrain::mineral(material)))?terrain::exposed([&](Cell d){return at(atWorld+d).material;}):0;
                const bool onFloor=terrain::mineral(material) && terrain::onFloor(air) && !at(atWorld+Sides[2]).exact;
                if(air==63 || onFloor){
                    terrain::stone(geometry,point,size,material,tint,key,onFloor);
                    continue;
                }
                for(int side=0;side<6;side++){
                    const auto neighbor=at(atWorld+Sides[side]);
                    if(neighbor.exact){
                        terrain::immediateBoundary(geometry,matter,{atWorld,level,material},side,area.lo.v()*size,tint);
                    }else{
                        if(neighbor.material && (neighbor.material!=Water || material==Water))continue;
                        if(bevel && terrain::beveled(air,side))
                            terrain::face(geometry,point,size,side,material,tint,key,air);
                        else faces.add(local,side,material);
                    }
                }

            }
    faces.draw(geometry,area.lo,size,0.78f,0.36f,1-0.027f*level,6);
    mesh.geometry.uploadTerrain(geometry);mesh.area=area;mesh.revision=revision;mesh.valid=true;
}

int Game::prepareMatterDetails(const World& matter,std::array<DetailMesh,FineDepth>& caches,
                               Vec eye,int depth,int limit,std::array<Cut,FineDepth>& cuts){
    const auto regions=detailBounds(eye,depth);
    for(int i=0;i<depth;i++){
        const double size=1.0/subdivisions(i+1);
        auto& cache=caches[i];
        auto desired=regions[i];
        if(cache.valid){
            const auto cameraCell=floorCell(eye/size);
            const Cell inset{5,5,5};
            if(Bounds{cache.area.lo+inset,cache.area.hi-inset}.contains(cameraCell))desired=cache.area;
        }
        const auto revision=matter.detailVersion(desired,i+1);
        if(!cache.valid || !sameBounds(cache.area,desired) || cache.revision!=revision){
            // Keep the coarser mesh in place until the next detail mesh is ready.
            if(limit==0)return i;
            finerMesh(matter,cache,desired,i+1,revision);
            --limit;
        }
        cuts[i]={desired.lo.v()*size,desired.hi.v()*size,size*0.19};
    }
    return depth;
}

void Game::drawMatterDetails(const std::array<DetailMesh,FineDepth>& caches,int depth,
                             const std::array<Cut,FineDepth>& cuts){
    for(int i=0;i<depth;i++){
        const auto& cache=caches[i];
        renderer.draw(cache.geometry,cache.area.lo.v()/subdivisions(i+1),1,0,
            i+1<depth?&cuts[i+1]:nullptr,0,0,i+1<depth?0.25f:0);
    }
}

void Game::makeRig(){
    Geometry b,r,f,cursor;
    b.box({-1.97,0.45,-1.29},{1.97,0.62,1.29},Steel);
    b.box({-1.97,0.63,-1.29},{1.97,0.67,1.29},CampTile);
    for(double x:{-1.72,1.72})for(double z:{-1.05,1.05})b.box({x-0.11,0,z-0.11},{x+0.11,0.45,z+0.11},Steel);
    for(double x:{-0.55,0.55}){
        b.box({x-0.16,0.67,-0.76},{x+0.16,0.78,0.85},Steel);
        b.box({x-0.06,0.77,-0.51},{x+0.06,1.15,-0.35},Iron);
        b.tube({x,0.78,0.67},{x,0.82,0.67},0.07,Iron,{0.82f,0.85f,0.83f},6);
    }
    b.box({-0.20,0.67,-0.2},{0.20,1.03,0.2},Steel);
    b.box({1.05,0.68,0.2},{1.68,0.87,0.71},Steel);
    b.box({1.12,0.869,0.28},{1.61,0.902,0.63},CampTile);
    b.tube({0.64,1.32,0.47},{1.12,0.91,0.47},0.025,Enamel);
    b.tube({0.64,1.32,-0.48},{1.36,0.84,-0.48},0.024,Enamel);
    b.tube({1.36,0.84,-0.48},{1.65,0.82,0.34},0.024,Enamel);
    r.tube({0,-0.34,0},{0,0.17,0},0.067,Iron,{1,1,1},10);
    r.tube({0,0.15,0},{0.42,0.15,0},0.044,Steel);
    r.tube({0.42,0.15,0},{0.42,0.33,0},0.052,Timber);
    r.box({-0.084,-0.08,0.0},{0.084,0.08,0.43},Magnet,{1.15f,0.60f,0.55f});
    r.box({-0.084,-0.08,-0.43},{0.084,0.08,0.0},Magnet,{0.66f,0.84f,1.25f});
    f.box({1.20,0.90,0.34},{1.53,0.927,0.59},Socket);
    constexpr double overhang=0.035;
    for(int x=0;x<2;x++)for(int y=0;y<2;y++)for(int z=0;z<2;z++){
        const Vec sign{double(1-2*x),double(1-2*y),double(1-2*z)};
        const Vec corner=Vec{double(x),double(y),double(z)}-sign*overhang;
        std::array<Vec,3> arms{corner,corner,corner};
        for(int axis=0;axis<3;axis++)arms[axis][axis]+=sign[axis]*0.13;
        for(int axis=0;axis<3;axis++)cursor.tube(arms[axis],arms[(axis+1)%3],0.009,Socket,white,4);
    }
    bench.upload(b);rotor.upload(r);fixture.upload(f);selectedBox.upload(cursor);
    Geometry portalShape,finalShape;
    micro::gate(portalShape);
    micro::gate(finalShape,true);
    portal.upload(portalShape);focalPortal.upload(finalShape);
    Geometry mote;mote.stone({},.015,GateRuneTexture,{.34f,.045f,.90f},4,6);gateMote.upload(mote);
    Geometry glow;
    glow.box({0.28,0.54,0.28},{0.72,0.81,0.72},Lamp);
    lampGlow.upload(glow);
    Geometry proxy;
    proxy.box({-1.95,0.42,-1.28},{1.95,0.66,1.28},Steel);
    for(double x:{-1.7,1.7})proxy.box({x-0.15,0,-1.15},{x+0.15,0.43,1.15},Steel);
    for(int i=0;i<16;i++){
        auto at=[](int index){double a=2*Pi*index/16;return Vec{0.64*std::cos(a),1.32+0.64*std::sin(a),0};};
        proxy.tube(at(i),at(i+1),0.11,Copper,{0.9f,0.91f,0.88f},4);
    }
    proxy.box({-0.16,0.66,-0.17},{0.16,1.32,0.17},Steel);
    benchDistant.upload(proxy);
    Geometry hardware;
    hardware.box({-0.15,0.53,-0.15},{0.15,0.82,0.15},Iron);
    hardware.tube({0,0.82,0},{0,1.66,0},0.10,Steel);
    hardware.box({0.02,1.28,0.25},{0.23,1.43,0.38},Socket);
    shaftMesh.upload(hardware);
    makeCoil();
}
void Game::makeCoil(){
    Geometry wire;
    usedTurns=generator.turns;
    const auto turns=std::min(45,usedTurns),steps=turns*32;
    const auto helix=[&](double t) {
        const double angle=t*2*Pi*turns;
        return Vec{0.64*std::cos(angle),1.32+0.64*std::sin(angle),-0.48+0.96*t};
    };
    for(int i=0;i<steps;i++)wire.tube(helix(i/double(steps)),helix((i+1)/double(steps)),
        std::min(0.031,0.43/turns),Copper,{1,0.96f,0.89f},6);
    coil.upload(wire);
}

void Game::makeCircuitMesh(){
    Geometry result;
    auto generate=[&](Cell cell,Device device){
        if(device.id==North)return;
        if(furniture(device.id)){
            expedition::decoration(result,device.id,cell.v(),!device.closed);
            return;
        }
        const Vec center=cell.v()+deviceCenter(device);
        Vec radius{0.30,0.30,0.30};
        for(int i=0;i<3;i++)if(device.normal[i])radius[i]=0.083;
        if(device.id==Cable)radius=radius*0.68;
        result.box(center-radius,center+radius,Steel);
        result.box(center-radius*0.70,center+radius+device.normal.v()*0.032,device.id==Socket?Socket:Cable);
        if(device.id==Lamp){
            const Vec ground=cell.v();
            result.box(ground+Vec{0.41,0.17,0.41},ground+Vec{0.59,0.61,0.59},Steel);
            result.box(ground+Vec{0.29,0.54,0.29},ground+Vec{0.71,0.80,0.71},Lamp);
        }
        const Vec ground=cell.v();
        if(device.id==SolarPanel){
            for(double x:{0.22,0.78})result.tube(ground+Vec{x,0.10,0.52},ground+Vec{x,0.56,0.52},0.045,Steel);
            for(int row=0;row<4;row++)for(int col=0;col<5;col++){
                const double x=0.14+col*0.147,z=0.13+row*0.18,y=0.31+z*0.53;
                result.quad(ground+Vec{x,y,z},ground+Vec{x,y+0.089,z+0.168},
                    ground+Vec{x+0.137,y+0.089,z+0.168},ground+Vec{x+0.137,y,z},Steel,{0.38f,0.74f,1.14f});
            }
        }else if(device.id==CrankGenerator){
            result.box(ground+Vec{0.12,0.11,0.13},ground+Vec{0.88,0.27,0.86},Timber);
            result.tube(ground+Vec{0.24,0.47,0.5},ground+Vec{0.76,0.47,0.50},0.22,Copper,{0.94f,0.63f,0.38f},12);
            result.tube(ground+Vec{0.69,0.64,0.5},ground+Vec{0.89,0.64,0.5},0.055,Steel);
        }else if(device.id==Quarry){
            result.box(ground+Vec{0.16,0.11,0.16},ground+Vec{0.84,0.25,0.84},Steel);
            for(double x:{0.19,0.70}){
                result.box(ground+Vec{x,0.23,0.22},ground+Vec{x+0.11,0.73,0.78},Steel);
                result.box(ground+Vec{x-0.025,0.66,0.20},ground+Vec{x+0.135,0.76,0.80},Copper);
            }
            result.box(ground+Vec{0.36,0.29,0.36},ground+Vec{0.64,0.56,0.64},Iron);
            result.tube(ground+Vec{0.5,0.26,0.5},ground+Vec{0.5,0.10,0.5},0.08,Copper);
            result.box(ground+Vec{0.34,0.69,0.37},ground+Vec{0.66,0.78,0.63},
                device.closed?Socket:Enamel);
        }else if(device.id==Fabricator){
            result.box(ground+Vec{0.17,0.14,0.17},ground+Vec{0.83,0.55,0.83},Copper);
            result.box(ground+Vec{0.17,0.53,0.17},ground+Vec{0.83,0.60,0.83},Steel);
            result.box(ground+Vec{0.23,0.60,0.66},ground+Vec{0.77,0.80,0.81},Steel);
            result.box(ground+Vec{0.37,0.74,0.64},ground+Vec{0.63,0.80,0.68},Socket);
        }else if(device.id==PrismPress){
            result.box(ground+Vec{0.17,0.12,0.17},ground+Vec{0.83,0.55,0.83},Steel);
            result.box(ground+Vec{0.27,0.37,0.23},ground+Vec{0.73,0.44,0.77},Copper);
            result.stone(ground+Vec{0.5,0.64,0.5},0.17,Socket,{0.85f,1.0f,1.2f},5,9);
        }else if(device.id==ResourceWell){
            result.tube(ground+Vec{0.5,0.13,0.5},ground+Vec{0.5,0.66,0.5},0.32,Steel,{0.79f,0.88f,0.91f},10);
            result.tube(ground+Vec{0.5,0.55,0.5},ground+Vec{0.5,0.69,0.5},0.35,Copper,{0.85f,0.73f,0.61f},10);
            result.stone(ground+Vec{0.5,0.68,0.5},0.18,device.closed?Socket:Enamel,{0.8f,0.99f,0.86f},6,10);
        }else if(device.id==HouseBuilder){
            for(double x:{0.23,0.77})for(double z:{0.24,0.76}){
                result.tube(ground+Vec{x,0.08,z},ground+Vec{x,0.66,z},0.047,Timber,{0.67f,0.50f,0.34f},6);
            }
            result.box(ground+Vec{0.15,0.61,0.16},ground+Vec{0.85,0.73,0.84},Timber);
            result.box(ground+Vec{0.22,0.73,0.60},ground+Vec{0.78,0.82,0.77},Copper);
            result.stone(ground+Vec{0.5,0.77,0.45},0.115,Steel,{0.94f,0.90f,0.75f},6,10);
        }
        if(device.id==Switch){
            const Vec ground=cell.v();
            result.box(ground+Vec{0.32,0.18,0.29},ground+Vec{0.68,0.40,0.71},Switch);
            double offset=device.closed?0.36:0.60;
            result.box(ground+Vec{0.39,0.38,offset},ground+Vec{0.61,0.52,offset+0.12},device.closed?Socket:Enamel);
        }
        if(!circuitPart(device.id))return;
        for(const auto& direction:Sides){
            Cell next=cell+direction;
            auto connected=devices.find(next);
            if(!(legacyRig && built && next==Port) && (connected==devices.end() || !circuitPart(connected->second.id)))continue;
            Device other=connected==devices.end()?Device{Socket,true,{0,1,0}}:connected->second;
            const Vec midpoint=(center+next.v()+deviceCenter(other))*0.5;
            result.tube(center,midpoint,0.049,Cable,{0.86f,0.95f,0.96f},5);
        }
    };
    if(built && legacyRig)generate(Port,{Socket,true,{0,1,0}});
    if(!devices.contains(CampStation))generate(CampStation,{Fabricator,true});
    const Vec table=CraftTable.v();
    for(double x:{0.15,0.83})for(double z:{0.15,0.83})
        result.tube(table+Vec{x,0.02,z},table+Vec{x,0.77,z},0.068,Timber,{0.69f,0.53f,0.37f},7);
    result.box(table+Vec{0.02,0.73,0.02},table+Vec{0.98,0.87,0.98},Timber,{0.94f,0.76f,0.56f});
    result.box(table+Vec{0.27,0.88,0.21},table+Vec{0.73,0.91,0.59},CampTile);
    result.tube(table+Vec{0.22,0.92,0.63},table+Vec{0.64,0.92,0.71},0.034,Steel);
    for(const auto& [cell,piece]:devices)generate(cell,piece);
    if(built && legacyRig){
        const Vec a=Rig+Vec{1.6,0.76,0.55},b=Rig+Vec{1.75,0.12,1.0};
        result.tube(a,b,0.036,Socket);result.tube(b,Port.v()+Vec{0.5,0.12,0.5},0.036,Socket);
    }
    grid.upload(result);dirtyGrid=false;
}

double Game::lightAt(Cell cell) const {
    const auto found=network.lamps.find(cell);
    return found==network.lamps.end()?0:generator.rms2*found->second*found->second/lampOhms;
}

void Game::drawTerrainSelection(const World& matter,Cell cell,int tier,int material,float glow){
    const double size=1.0/subdivisions(tier);
    if(target && !armed && sculptable(material)){
        Vec normal=target->surfaceNormal;
        if(length(normal)<0.5)normal=target->normal.v();
        if(length(normal)<0.5)normal={0,1,0};
        if(!shovelOutline.vertices || length(normal-outlineNormal)>0.035){
            const Vec tangent=unit(cross(normal,std::abs(normal.y)<0.9?Vec{0,1,0}:Vec{0,0,1}));
            const Vec across=cross(normal,tangent);
            Geometry outline;
            const auto rim=[&](double a){return (tangent*std::cos(a)+across*std::sin(a))*ShovelRadius;};
            for(int i=0;i<16;i++)outline.tube(rim(i*Pi/8),rim((i+0.64)*Pi/8),0.005,Socket,white,4);
            shovelOutline.upload(outline);
            outlineNormal=normal;
        }
        const Vec hit=camera()+viewDirection(yaw,pitch)*target->distance;
        renderer.draw(shovelOutline,hit+normal*(0.012*size),size,0,nullptr,glow);
        return;
    }
    const Vec corner=cell.v()*size;
    const auto* slope=matter.leafRoof({cell,tier,material});
    if(!slope){
        renderer.draw(selectedBox,corner,size,0,nullptr,glow);
        return;
    }
    const auto root=parent(cell,subdivisions(tier));
    constexpr std::array<Vec,4> corners={{{0,0,0},{1,0,0},{1,0,1},{0,0,1}}};
    std::array<double,4> heights{};
    for(std::size_t i=0;i<corners.size();i++){
        const auto point=corner+corners[i]*size-root.v();
        heights[i]=std::clamp((slope->height(point.x,point.z)-corner.y)/size,0.0,1.0);
    }
    if(!slopeSelection.vertices || selectionHeights!=heights){
        Geometry outline;
        for(std::size_t i=0;i<corners.size();i++){
            const auto next=(i+1)%corners.size();
            const auto a=corners[i]+Vec{0,heights[i]+0.018,0};
            const auto b=corners[next]+Vec{0,heights[next]+0.018,0};
            outline.tube(a,a+(b-a)*0.24,0.007,Socket,white,4);
            outline.tube(a+(b-a)*0.76,b,0.007,Socket,white,4);
        }
        slopeSelection.upload(outline);
        selectionHeights=heights;
    }
    renderer.draw(slopeSelection,corner,size,0,nullptr,glow);
}

void Game::drawGate(Vec origin,double scale,bool final){
    renderer.draw(final?focalPortal:portal,origin,scale);
    if(renderer.graphics().detail==0 || length(camera()-origin)>22*scale)return;
    const int count=renderer.graphics().detail>=2?16:8;
    for(int i=0;i<count;i++){
        const double t=std::fmod(playTime*.28+i*.61803398875,1.),a=2*Pi*(i*.382+playTime*.11);
        const double rad=(.38+.67*t)*scale;
        const Vec p=origin+Vec{cos(a)*rad,(.16+2.1*t)*scale,.64*sin(a)*rad};
        const double pulse=std::max(.12,sin(t*Pi));
        renderer.draw(gateMote,p,scale*pulse,0,nullptr,1.2);
    }
}

void Game::drawField(){
    if(usedTurns!=generator.turns)makeCoil();
    if(dirtyGrid)makeCircuitMesh();
    const Vec eye=camera();
    const Vec dir=viewDirection(yaw,pitch);
    const Vec right=unit(cross(dir,{0,1,0}));
    const double scale=body();
    const bool underground=feet.y<23&&feet.x>63&&feet.x<83&&std::abs(feet.z-24)<6;
    View view{eye,dir,right,cross(right,dir)};
    view.near=std::max(2e-7,scale*0.034);
    view.detailBody=scale;
    view.far=200;view.fog=std::lerp(122.0,37.0,std::min(1.0,zoom/2));
    if(modernWorld && precision>=4){
        view.fog=std::lerp(37.0,22.0,double(precision-4)/(FineDepth-4));
        view.far=64;
    }
    // Draw radius is bounded by the streamed terrain system. Far extends fog
    // within that radius; it never requests unbounded synchronous tiles.
    view.fog*=std::array<double,4>{.48,.75,1.0,1.20}[renderer.graphics().distance];
    view.far=std::max(view.far,view.fog+20);
    view.ambient=night?0.17f:underground?0.39f:0.95f;
    view.sky=night?Color{0.021f,0.038f,0.065f}:Color{0.36f,0.58f,0.66f};
    view.skyMode=underground?0:night?2:1;
    view.forestLighting=modernWorld && world.seed && world.sculptedTerrain && !night && !underground && precision<=2;
    std::array<Light,8> lamps{};
    if(modernWorld && !underground){
        const auto solar=solarcycle::at(solarClock);
        view.dayCycle=true;view.sun=solar.sun;view.shadowSun=solarcycle::shadowSun(solarClock);
        view.daylight=float(solar.day);view.twilight=float(solar.twilight);
        view.shadowWeight=float(solarcycle::smooth(.08,.32,solar.sun.y));
        view.ambient=float(std::lerp(.115,.95,solar.day));
        const Color dark{.021f,.038f,.065f},bright{.36f,.58f,.66f};
        view.sky={float(std::lerp(dark.r,bright.r,solar.day)+.12*solar.twilight),
                  float(std::lerp(dark.g,bright.g,solar.day)+.025*solar.twilight),
                  float(std::lerp(dark.b,bright.b,solar.day))};
        view.skyMode=1;
        view.forestLighting=world.seed && world.sculptedTerrain && precision<=2 &&
            view.shadowWeight>0 && view.shadowSun.y>.04;
    }
    std::size_t lampCount=0;
    if(built&&(night||underground)){
        std::vector<std::pair<double,Cell>> nearby;
        for(const auto& [cell,ratio]:network.lamps){
            if(ratio<=0 || lightAt(cell)<0.13)continue;
            double d=length(cell.v()-eye);
            if(d<15)nearby.emplace_back(d,cell);
        }
        std::sort(nearby.begin(),nearby.end());
        for(std::size_t n=0;n<std::min<std::size_t>(nearby.size(),4);n++){
            auto cell=nearby[n].second;
            lamps[lampCount++]={cell.v()+Vec{0.5,0.70,0.5},float(std::clamp(lightAt(cell)*0.93,0.0,2.2))};
        }
    }
    if(modernWorld && world.seed && world.sculptedTerrain && !underground){
        const Cell tile=parent(floorCell(eye),32);
        std::vector<std::pair<double,Vec>> nearby;
        for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx)
            for(Vec p:cachedTrail({tile.x+dx,0,tile.z+dz})){
                p.y+=1.90;const double d=length(p-eye);
                if(d<27)nearby.emplace_back(d,p);
            }
        std::sort(nearby.begin(),nearby.end(),[](const auto& a,const auto& b){return a.first<b.first;});
        const float intensity=float(std::lerp(3.0,.48,view.daylight));
        for(const auto& [distance,p]:nearby){
            if(lampCount==lamps.size())break;
            lamps[lampCount++]={p,intensity,true};
        }
    }
    view.torchRange=flashlight?float(25*body()):0;
    flashLight(lamps);
    view.effectTime=playTime;
    for(const auto& gate:gates)if(gate && length(*gate-eye)<45)view.gateLights[view.gateLightCount++]=*gate+Vec{0,1,0};
    renderer.start(view,fieldWidth,fieldHeight,lamps);
    std::array<Cut,FineDepth> cuts{};
    const int visibleDepth=prepareMatterDetails(world,detail,eye,precision,
        (modernWorld && precision>0) || std::abs(goalZoom-zoom)>0.10?1:2,cuts);
    const Cell current=parent(floorCell(feet),32);
    const int depth=int(feet.y);
    std::erase_if(chunks,[&](const auto& pair){
        return std::abs(pair.first.x-current.x)>3 || std::abs(pair.first.z-current.z)>3;
    });
    std::erase_if(trailCache,[&](const auto& pair){
        return std::abs(pair.first.x-current.x)>3 || std::abs(pair.first.z-current.z)>3;
    });
    const int drawRadius=view.fog>64 ? 3 : 2;
    if(drawRadius==3)world.streamInteractive(floorCell(feet),drawRadius);
    const double aspect=double(fieldWidth)/fieldHeight;
    const auto inView=[&](Cell tile){
        const Vec corner{tile.x*32.0,0,tile.z*32.0};
        const double padding=modernWorld && precision>=4?8.0:24.0;
        return view.sees({corner,corner+Vec{32,double(world.heightLimit()),32}},aspect,view.fog+padding);
    };
    int rebuilds=0;
    const int rebuildBudget=modernWorld?1:std::abs(goalZoom-zoom)>0.1?1:2;
    // Build visible chunks first, so the shadow and color passes use the
    // same meshes. Cached off-screen trees also cast into the visible scene.
    std::vector<std::pair<Cell,bool>> visibleTiles;
    for(int radius=0;radius<=drawRadius;radius++)for(int dz=-radius;dz<=radius;dz++)for(int dx=-radius;dx<=radius;dx++){
        if(std::max(std::abs(dx),std::abs(dz))!=radius)continue;
        Cell tile{current.x+dx,0,current.z+dz};
        if(tile.x<-64 || tile.x>=64 || tile.z<-64 || tile.z>=64)continue;
        if(radius && !inView(tile))continue;
        auto& mesh=chunks[tile];
        const bool outer=radius>2;
        const bool edited=mesh.depth>=0 && mesh.version!=world.meshVersion(tile);
        const bool remake=mesh.depth<0 || edited ||
            (!outer && (mesh.proxyOnly || depth+4<mesh.depth));
        bool ready=true;
        if(modernWorld && world.seed && world.sculptedTerrain)
            for(int az=-1;az<=1;++az)for(int ax=-1;ax<=1;++ax)
                ready=ready && world.tileReady(tile.x+ax,tile.z+az);
        // No synchronous far-chunk generation from the draw thread. Rebuild
        // at most the frame budget, including edits at shared tile corners.
        if(remake && ready && rebuilds<rebuildBudget){
            terrainMesh(mesh,tile,depth,outer);rebuilds++;
        }
        if(mesh.depth<0 || (!radius && !inView(tile)))continue;
        const double nearX=std::max({0.0,tile.x*32.0-eye.x,eye.x-(tile.x+1)*32.0});
        const double nearZ=std::max({0.0,tile.z*32.0-eye.z,eye.z-(tile.z+1)*32.0});
        const bool distant=outer || mesh.proxyOnly || std::hypot(nearX,nearZ)>(precision?18:42);
        visibleTiles.emplace_back(tile,distant);
    }
    if(view.forestLighting){
        // Stable, order-independent revision: any changed or newly streamed
        // caster invalidates the cached map. Camera rotation alone does not.
        std::uint64_t revision=*world.seed;
        for(const auto& [tile,mesh]:chunks){
            if(mesh.depth<0 || !mesh.trees.vertices)continue;
            const std::uint64_t key=CellHash{}(tile);
            revision+=((key<<32)|mix(std::uint32_t(mesh.treeVersion)^std::uint32_t(mesh.trees.vertices))) * 0x9e3779b97f4a7c15ull;
        }
        if(renderer.beginCanopy(revision)){
            for(const auto& [tile,mesh]:chunks){
                if(mesh.depth<0)continue;
                const Vec origin{double(tile.x*32),0,double(tile.z*32)};
                if(renderer.canopyIntersects({origin-Vec{1,1,1},origin+Vec{33,double(world.heightLimit()+1),33}}))
                    renderer.shadowTree(mesh.trees,origin);
            }
            renderer.endCanopy();
        }
    }
    // Independent close-range map: the full animated skeleton is submitted
    // without rebuilding the cached forest shadow map every simulation tick.
    if(view.forestLighting && renderer.graphics().shadows>=2){
        const bool castWitness=witness.active && witness.rig.ready && witness.level==level && witness.family==family &&
            length(witness.feet-eye)<42*witness.scale;
        std::optional<Vec> north;
        for(const auto& [cell,dev]:devices)if(dev.id==North){const Vec p=northOrigin(cell);if(length(p-eye)<20)north=p;}
        std::optional<Vec> localCenter;
        double radius=7.;
        if(castWitness){localCenter=witness.feet+Vec{0,2,0}*witness.scale;radius*=witness.scale;}
        else if(north)localCenter=*north+Vec{0,1,0};
        else for(const auto& gate:gates)if(gate && length(*gate-eye)<14){localCenter=*gate+Vec{0,1,0};break;}
        if(localCenter && renderer.beginCreatureShadow(*localCenter,radius)){
            if(castWitness){witnessBody.upload(true);
                for(const auto& part:witnessPose())renderer.shadowCreature(part.mesh->render,part.origin,witness.scale,part.heading,part.tilt);}
            if(north && length(*north-*localCenter)<radius+2){
                const int lod=prepareNorth(length(*north-eye));
                renderer.shadowCreature(northModels[lod],*north,1,northHome?northHeading:std::atan2(eye.x-north->x,eye.z-north->z),0);
            }
            for(const auto& gate:gates)if(gate && length(*gate-*localCenter)<radius+2)renderer.shadowCreature(portal,*gate,1.05,0,0);
            renderer.endCreatureShadow();
        }
    }
    renderer.faceCulling(true);
    for(const auto& [tile,distant]:visibleTiles){
        auto& mesh=chunks.at(tile);
        const Vec location{double(tile.x*32),0,double(tile.z*32)};
        renderer.draw(distant?mesh.proxy:mesh.terrain,location,
            1,0,visibleDepth?&cuts[0]:nullptr,0,0,distant?1.5f:visibleDepth?0.65f:0);
        if(!distant)renderer.draw(mesh.contours,location);
        renderer.draw(mesh.trees,location,1,0,visibleDepth?&cuts[0]:nullptr);
        if((!distant || renderer.graphics().foliage==3) && mesh.leafSprigs.vertices && renderer.graphics().foliage>0 &&
            (renderer.graphics().foliage>=2 || length(eye-location-Vec{16,0,16})<38)){
            renderer.faceCulling(false);
            renderer.draw(mesh.leafSprigs,location,1,0,visibleDepth?&cuts[0]:nullptr);
            renderer.faceCulling(true);
        }
        renderer.draw(mesh.exact,location,1,0,nullptr,0,precision?0.14f:0);
        renderer.draw(mesh.trailPosts,location);
        renderer.draw(mesh.trailFlames,location,1,0,nullptr,float(std::lerp(1.8,.65,double(view.daylight))));
    }
    drawMatterDetails(detail,visibleDepth,cuts);
    renderer.faceCulling(false);
    if(built){
        if(legacyRig && length(eye-Rig)<105){
            const double distance=length(eye-Rig);
            if(distance>(precision?4.7:36))renderer.draw(benchDistant,Rig,1,0,nullptr,0,0,1);
            else {
                renderer.draw(bench,Rig);renderer.draw(coil,Rig);
                renderer.draw(generator.field?rotor:shaftMesh,generator.field?Rig+Vec{0,1.32,0}:Rig,1,generator.field?generator.angle:0);
                if(generator.test && generator.rms2>0.3)renderer.draw(fixture,Rig,1,0,nullptr,0.95f);
            }
        }
        for(const auto& [cell,ratio]:network.lamps){
            if(ratio<=0 || lightAt(cell)<0.13 || length(cell.v()-eye)>55)continue;
            const float powered=float(std::clamp(lightAt(cell)*0.52,0.35,1.8));
            renderer.draw(lampGlow,cell.v(),1,0,nullptr,powered);
        }
    }
    renderer.draw(grid);
    drawNorth(view);
    drawGoblin(view);
    drawWitness();
    drawHorror(view);
    for(const auto gate:gates)if(gate && length(*gate-eye)<45)
        drawGate(*gate,1.05);
    if(gateTarget && gates[*gateTarget])renderer.draw(selectedBox,*gates[*gateTarget]-Vec{0.5,0,0.5},1,0,nullptr,0.75f);
    else if(machineConsole)renderer.draw(selectedBox,machineConsole->v(),1,0,nullptr,0.75f);
    else if(deviceTarget)renderer.draw(selectedBox,deviceTarget->v(),1,0,nullptr,0.75f);
    else if(target && precision<=2){
        const auto cell=parent(target->cell,subdivisions(target->level-precision));
        drawTerrainSelection(world,cell,precision,target->id,1.0f);
    }
}

namespace {
void panel(Renderer& r,float x,float y,float width,float height) {
    r.rect(x,y,width,height,dark);
    r.line(x,y,width,1,rule);r.line(x,y+height-1,width,1,rule);
    r.line(x,y,1,height,rule);r.line(x+width-1,y,1,height,rule);
    if(width>190 && height>82){
        const Color inset{0.65f,0.51f,0.31f,0.36f};
        r.line(x+4,y+4,width-8,1,inset);r.line(x+4,y+height-5,width-8,1,inset);
        for(float corner:{x+4,x+width-9}){
            r.rect(corner,y+4,5,5,rule);
            r.rect(corner,y+height-9,5,5,rule);
        }
    }
}
constexpr Color goldInk{0.99f,0.85f,0.62f},creamInk{0.99f,0.91f,0.78f},
    menuInk{0.17f,0.100f,0.062f},menuMuted{0.31f,0.215f,0.14f},menuAccent{0.41f,0.095f,0.07f};
void menuFrame(Renderer& r,float x,float y,float w,float h,float s,std::string_view title){
    const Color edge{0.65f,0.42f,0.21f},shadow{0.055f,0.035f,0.024f,0.80f};
    const Color leather{0.16f,0.082f,0.054f,0.94f};
    r.rect(x+w,y+8*s,6*s,h,shadow);
    r.rect(x+6*s,y+h,w,8*s,shadow);
    r.rect(x,y,w,43*s,leather);
    r.rect(x,y+h-9*s,w,9*s,leather);
    r.rect(x,y+43*s,9*s,h-52*s,leather);
    r.rect(x+w-9*s,y+43*s,9*s,h-52*s,leather);
    r.vellum(x+9*s,y+42*s,w-18*s,h-51*s,{0.89f,0.73f,0.52f,0.86f});
    r.rect(x+11*s,y+7*s,w-22*s,33*s,{0.12f,0.072f,0.046f,0.91f});
    r.line(x+12*s,y+46*s,w-24*s,1.5f*s,edge);
    for(float inset:{0.f,4.f,9.f}){
        const float d=inset*s,t=std::max(1.f,(inset==4?2.f:1.f)*s);
        const auto color=inset==4?edge:Color{0.86f,0.63f,0.33f,0.90f};
        r.line(x+d,y+d,w-2*d,t,color);
        r.line(x+d,y+h-d-t,w-2*d,t,color);
        r.line(x+d,y+d,t,h-2*d,color);
        r.line(x+w-d-t,y+d,t,h-2*d,color);
    }
    for(float px:{x+9*s,x+w-9*s})for(float py:{y+9*s,y+h-9*s}){
        r.diamond(px,py,8*s,8*s,edge);
        r.diamond(px,py,4*s,4*s,goldInk);
        r.diamond(px,py,2*s,2*s,shadow);
    }
    const float titleX=x+23*s;
    for(float stem:{x+4*s,x+w-4*s}){
        r.diamond(stem,y+h*0.5f,4.5f*s,13*s,edge);
        r.diamond(stem,y+h*0.5f,2.3f*s,5*s,goldInk);
    }
    r.title(titleX,y+9*s,title,goldInk,1.05f*s);
    r.diamond(x+w-24*s,y+24*s,7*s,5*s,edge);
    r.diamond(x+w-24*s,y+24*s,2.5f*s,2.5f*s,goldInk);
}
void paragraph(Renderer& r,float x,float y,float width,std::string_view text,Color tint,float size) {
    std::istringstream words{std::string(text)};
    std::string word,line;
    while(words>>word){
        if(!line.empty() && r.textWidth(line+" "+word,size)>width){r.text(x,y,line,tint,size);y+=23*size;line.clear();}
        if(!line.empty())line+=" ";
        line+=word;
    }
    if(!line.empty())r.text(x,y,line,tint,size);
}
std::string bearing(double yaw) {
    const int degrees=(int(std::lround(yaw*180/Pi))+360)%360;
    constexpr std::array names={"N","NE","E","SE","S","SW","W","NW"};
    return std::string(names[((degrees+22)/45)%8])+" "+std::to_string(degrees);
}
}

void Game::drawCraft(float s){
    const float w=740*s,h=575*s,x=(fieldWidth-w)*0.5f,y=(fieldHeight-h)*0.5f;
    constexpr std::array titles={"Workbench","Machine Crafter","Prism Press","House Builder"};
    menuFrame(renderer,x,y,w,h,s,titles[int(workshop)]);
    const bool crafterInRange=nearCrafter();
    const bool atCamp=camp();
    const bool press=level<0 && std::any_of(devices.begin(),devices.end(),[&](const auto& entry){
        return entry.second.id==PrismPress && length(entry.first.v()-feet)<6;
    });
    const int product=recipes[recipe].makes;
    const char* instructions=workshop==Workshop::House
        ?"Connect the bench to the generator with cable. Minimum power is 25 mW."
        :prismRecipe(product)
        ?(press?"Prism Press in range.":"Mount a Prism Press within 6 m.")
        :machineRecipe(product)
        ?(crafterInRange?"Machine Crafter in range.":"Mount a Machine Crafter within 6 m.")
        :(atCamp?"Select a recipe with the arrow keys.":"Return to camp to craft this recipe.");
    renderer.text(x+23*s,y+52*s,instructions,menuInk,0.76f*s);
    std::vector<int> available;
    for(int i=0;i<int(recipes.size());i++)if(visibleRecipe(i))available.push_back(i);
    const auto selectedRow=std::find(available.begin(),available.end(),recipe)-available.begin();
    const auto first=std::clamp(int(selectedRow)-8,0,std::max(0,int(available.size())-15));
    for(int rowIndex=0;rowIndex<15 && first+rowIndex<int(available.size());rowIndex++){
        const int i=available[first+rowIndex];
        const auto& r=recipes[i];
        const float row=y+(86+rowIndex*27)*s;
        if(i==recipe)renderer.rect(x+17*s,row,w-34*s,26*s,{0.40f,0.25f,0.12f,0.93f});
        bool eligible=(r.kiln?bag[Kiln]>0:true) && (workshop==Workshop::House?workshopCell && housePower(*workshopCell)>=0.025:
            prismRecipe(r.makes)?press:machineRecipe(r.makes)?crafterInRange:atCamp);
        std::string ingredients;
        for(const auto& [id,n]:r.cost){
            if(!ingredients.empty())ingredients+="  /  ";
            ingredients+=std::to_string(n)+" "+materialName(id);
            eligible=eligible && bag[id]>=std::int64_t(n)*FullBlock;
        }
        renderer.text(x+26*s,row,r.label,i==recipe?goldInk:menuInk,0.88f*s);
        renderer.text(x+330*s,row+2*s,ingredients,i==recipe?creamInk:eligible?menuInk:menuMuted,0.67f*s);
    }
    renderer.line(x+20*s,y+h-50*s,w-40*s,1,rule);
    renderer.text(x+25*s,y+h-39*s,"ENTER  Craft        ESC  Close",menuInk,0.82f*s);
    if(available.size()>15){
        const float bar=405*s*15/available.size();
        const float at=86*s+405*s*first/available.size();
        renderer.rect(x+w-15*s,y+86*s,2*s,405*s,rule);
        renderer.rect(x+w-16*s,y+at,4*s,bar,goldInk);
    }
    if(statusTime>0)paragraph(renderer,x+285*s,y+h-42*s,w-315*s,status,menuAccent,0.75f*s);
}
void Game::drawMaterials(float s){
    const float w=590*s,h=407*s,x=(fieldWidth-w)*0.5f,y=(fieldHeight-h)*0.5f;
    menuFrame(renderer,x,y,w,h,s,"Build Materials");
    renderer.text(x+21*s,y+50*s,"One resource places one cube at your current scale.",menuInk,0.81f*s);
    const int first=std::clamp((picker/2-3)*2,0,int(buildPalette.size())-11);
    for(int i=first;i<std::min(first+12,int(buildPalette.size()));i++){
        const int row=i-first;
        const float xx=x+(21+(row%2)*277)*s,yy=y+(87+(row/2)*36)*s;
        const int id=buildPalette[i];
        if(i==picker)renderer.rect(xx,yy,262*s,31*s,{0.44f,0.28f,0.14f,0.91f});
        renderer.text(xx+9*s,yy+2*s,materialName(id),i==picker?goldInk:menuInk,0.84f*s);
        renderer.text(xx+180*s,yy+2*s,amount(bag[id]),i==picker?creamInk:bag[id]?menuInk:menuMuted,0.78f*s);
    }
    renderer.line(x+21*s,y+h-49*s,w-42*s,1,rule);
    renderer.text(x+21*s,y+h-39*s,"ARROWS  Move      ENTER  Select      H / ESC  Close",menuMuted,0.79f*s);
}
void Game::drawMap(float s){
    const float w=390*s,h=414*s,ax=(fieldWidth-w)*0.5f,ay=(fieldHeight-h)*0.5f;
    menuFrame(renderer,ax,ay,w,h,s,"Survey Map");
    const float x=ax+18*s,y=ay+48*s,side=w-36*s;
    const auto at=parent(floorCell(feet),32);
    const double extent=std::clamp(std::max(std::abs(at.x),std::abs(at.z))+6,8,64);
    const float step=side/float(2*extent);
    auto point=[&](Vec pos){return std::pair{x+(float(pos.x)/32+extent)*step,y+(float(pos.z)/32+extent)*step};};
    renderer.rect(x,y,side,side,{0.06f,0.105f,0.135f});
    for(int i=0;i<=16;i++){
        renderer.line(x+side*i/16,y,1,side,rule);
        renderer.line(x,y+side*i/16,side,1,rule);
    }
    for(const auto& [cell,seen]:visited){
        if(!seen || std::abs(cell.x)>=extent || std::abs(cell.z)>=extent)continue;
        auto [px,py]=point(cell.v()*32);
        renderer.rect(px+1,py+1,step-2,step-2,{0.24f,0.48f,0.37f});
    }
    auto [qx,qy]=point({57,0,24});
    auto [cx,cy]=point({0,0,-7});
    auto [px,py]=point(feet);
    renderer.rect(cx-3*s,cy-3*s,6*s,6*s,amber);
    renderer.rect(qx-3*s,qy-3*s,6*s,6*s,{0.76f,0.59f,0.84f});
    for(const auto& gate:gates)if(gate){
        const Vec entrancePoint=*gate;
        auto [ex,ey]=point(entrancePoint);
        renderer.rect(ex-2*s,ey-2*s,5*s,5*s,mint);
    }
    renderer.rect(px-2*s,py-2*s,5*s,5*s,white);
    renderer.text(ax+17*s,ay+h-22*s,"Camp / Quarry / Gates / You    Esc close",menuMuted,0.75f*s);
}
void Game::drawHud(){
    const float width=fieldWidth,height=fieldHeight;
    const float s=std::clamp(std::min(width/1280.0f,height/720.0f),0.63f,2.2f);
    const float m=24*s;
    renderer.rect(0,0,width,43*s,{0.032f,0.065f,0.081f,0.94f});
    renderer.line(0,42*s,width,1,rule);
    renderer.rect(m,13*s,14*s,14*s,mint);
    renderer.title(m+22*s,5*s,"!Scale",goldInk,1.03f*s);
    renderer.text(m+135*s,10*s,"Delbert",creamInk,0.83f*s);
    renderer.text(m+240*s,10*s,surfaceFortress?"FINAL SHRINK TIER":level>=0?"BELOW THE SURFACE":"IN THE FIELD",soft,0.78f*s);
    const auto heading=bearing(yaw);
    renderer.text((width-renderer.textWidth(heading,s))*0.5f,6*s,heading,mint,s);
    const double rate=timeRate();
    std::ostringstream rateText;
    if(rate<0.0005)rateText<<std::scientific<<std::setprecision(1)<<rate;
    else rateText<<std::fixed<<std::setprecision(3)<<rate;
    const std::string seconds=number(playTime,1)+" s   "+rateText.str()+"x";
    renderer.text(width-m-renderer.textWidth(seconds,0.89f*s),8*s,seconds,white,0.89f*s);
    const float px=m,py=64*s;
    const double heightMeters=surfaceFortress?body():level>=0?micro::details(level).cellMeters*innerSize():body();
    renderer.text(px,py,meters(hullHeight()*heightMeters)+" tall",creamInk,0.88f*s);
    if(level>=0)renderer.text(px,py+26*s,micro::details(level).name,soft,0.78f*s);
    if(inFortress()){
        int alive=0;for(int i=0;i<guardCount();i++){const auto& g=guards[guardId(i)];alive+=!g.ready || g.health>0;}
        renderer.text(px,py+48*s,"Garrison "+std::to_string(alive)+" / 15",creamInk,.75f*s);
        const auto& brood=broods[family];if(brood.slot>=0)renderer.text(px,py+67*s,"Brood "+std::to_string(int(brood.age/settlement::BroodDuration*100))+"%",soft,.75f*s);
    }
    if(witness.active && witness.family==family && witness.level==level && length(witness.feet-camera())<24*witness.scale){
        const float w=220*s,x=(width-w)*.5f,y=51*s;
        renderer.rect(x,y,w,28*s,dark);renderer.rect(x+3*s,y+19*s,(w-6*s)*witnessHealth/WitnessMaxHealth,5*s,{.65f,.20f,.18f,1});
        renderer.text(x+7*s,y+2*s,"god.  "+std::to_string(witnessHealth)+" / 200",creamInk,.74f*s);
    }
    const float cx=width*0.5f,cy=height*0.5f;
    if(hurtCooldown>0.3){
        const Color damage{0.65f,0.03f,0.02f,float((hurtCooldown-0.3)*0.72)};
        renderer.rect(0,43*s,12*s,height-76*s,damage);
        renderer.rect(width-12*s,43*s,12*s,height-76*s,damage);
    }
    if(wantsCapture()){
        // The integral amber optic is the only aiming reticle for the rifle.
        // Preserve the interaction cursor when holstered or using a pistol.
        if(!(armed && weapon==PulseRifle)){
            renderer.line(cx-10*s,cy,7*s,1.5*s,white);renderer.line(cx+4*s,cy,7*s,1.5*s,white);
            renderer.line(cx,cy-10*s,1.5*s,7*s,white);renderer.line(cx,cy+4*s,1.5*s,7*s,white);
        }
        std::string hit;
        if(gateTarget)hit=materialName(*gateTarget==0?MeadowGate:QuartzGate);
        else if(deviceTarget){
            const auto d=devices.find(*deviceTarget);
            hit=d!=devices.end()?materialName(d->second.id):
                *deviceTarget==CampStation?"Camp Machine Crafter":*deviceTarget==CraftTable?"Crafting table":"Generator terminal";
            if(d!=devices.end() && d->second.id==Quarry){
                const auto load=network.quarries.find(d->first);
                hit+="  "+std::string(materialName(d->second.material));
                hit+=!d->second.closed?"  Paused":load==network.quarries.end()?"  Unpowered":
                    "  "+number(generator.rms2*load->second*load->second/quarryOhms,1)+" W";
            }
            if(d!=devices.end() && d->second.id==SolarPanel)hit+=night?"  Offline":"  Charging";
        }
        else if(target){
            hit=materialName(target->id);
            if(level<0 && precision>0)hit+="  "+meters(1.0/subdivisions(precision));
        }
        if(!hit.empty()){
            const float tw=renderer.textWidth(hit,0.83f*s);
            renderer.rect(cx-tw*0.5f-7*s,cy+21*s,tw+14*s,24*s,dark);
            renderer.text(cx-tw*0.5f,cy+21*s,hit,white,0.83f*s);
        }
    }
    float strip=height-104*s;
    if(level>=0 || health<100 || armed || bag[PowerPack]){
        const float xx=width-m-251*s,yy=height-209*s;
        panel(renderer,xx,yy,251*s,101*s);
        renderer.text(xx+13*s,yy+6*s,"Delbert    "+std::to_string(health)+"/100",
            hurtCooldown>0?Color{1,0.49f,0.40f}:creamInk,0.81f*s);
        renderer.rect(xx+13*s,yy+34*s,225*s,5*s,rule);
        renderer.rect(xx+13*s,yy+34*s,225*s*health/100,5*s,{0.82f,0.29f,0.24f});
        if(armed)renderer.text(xx+13*s,yy+47*s,std::string(weapon==PulseRifle?"Rifle  ":"Pistol  ")+amount(bag[PulseRounds])+" rounds",white,0.67f*s);

        if(bag[PowerPack])renderer.text(xx+13*s,yy+70*s,
            "Battery "+number(battery,0)+"/1000"+(armed && weapon==PulseRifle?"  Integrated optic":""),goldInk,0.67f*s);
    }
    const auto item=placement[selected];
    panel(renderer,cx-100*s,height-67*s,200*s,49*s);
    renderer.text(cx-87*s,height-64*s,materialName(item),creamInk,0.96f*s);
    renderer.text(cx-87*s,height-40*s,amount(bag[item])+" owned",soft,0.71f*s);
    if(inventory){
        const float w=656*s,h=515*s,x=(width-w)/2,y=(height-h)/2;
        menuFrame(renderer,x,y,w,h,s,"Inventory");
        int row=0;
        for(int id=1;id<ItemLimit;id++)if(bag[id]>0){
            const float xx=x+25*s+(row/16)*208*s,yy=y+63*s+(row%16)*25*s;
            renderer.text(xx,yy,std::string(materialName(id))+"  "+amount(bag[id]),id==item?goldInk:menuInk,0.70f*s);
            row++;
        }
        if(!row)renderer.text(x+25*s,y+65*s,"Backpack is empty",menuInk,0.88f*s);
    }
    if(statusTime>0 && !crafting && !materials && !machineConsole){
        const float tw=renderer.textWidth(status,0.86f*s),y=strip-38*s;
        renderer.rect(cx-tw/2-12*s,y,tw+24*s,30*s,dark);
        renderer.text(cx-tw/2,y,status,amber,0.86f*s);
    }
    if(debug){
        const std::string info=(frameSeconds>0?number(1/frameSeconds,0)+" FPS   ":"")+
            "MESH "+std::to_string(chunks.size())+"   LEVEL "+std::to_string(precision)+
            "   EDITS "+std::to_string(world.edits().size());
        renderer.text(m,45*s,info+"   3D "+std::to_string(renderer.renderScale())+"%",white,0.63f*s);
        const double reach=20*(level<0?body():innerSize());
        const double presence=witnessHit(camera(),viewDirection(yaw,pitch),reach);
        if(presence<reach)renderer.text(m,59*s,"god.",amber,0.65f*s);
    }
    if(map)drawMap(s);
    if(crafting)drawCraft(s);
    if(materials)drawMaterials(s);
    if(machineConsole)drawMachine(s);
    if(electrical){
        const float w=496*s,h=240*s,x=(width-w)/2,y=(height-h)/2;
        menuFrame(renderer,x,y,w,h,s,"Generator Controls");
        renderer.text(x+22*s,y+62*s,"UP/DOWN      "+number(generator.rpm,0)+" rpm target",menuInk,0.91f*s);
        renderer.text(x+22*s,y+95*s,"LEFT/RIGHT   "+std::to_string(generator.turns)+" coil turns",menuInk,0.91f*s);
        renderer.text(x+22*s,y+128*s,keyName(platform::I)+"            Test load "+(generator.test?"connected":"open"),menuInk,0.91f*s);
        renderer.text(x+22*s,y+161*s,keyName(platform::M)+"            "+(generator.field==0?"Magnet removed":generator.field>0.1?"Strong magnet":"Lodestone magnet"),menuInk,0.91f*s);
        renderer.text(x+22*s,y+198*s,"ENTER / ESC   Back",menuMuted,0.85f*s);
    }
    if(settingsPage)drawSettings(s);
    else if(paused){
        const float w=440*s,h=(worldTitle.empty()?263:335)*s,x=(width-w)/2,y=(height-h)/2;
        menuFrame(renderer,x,y,w,h,s,"!Scale");
        renderer.text(x+w-240*s,y+11*s,"v0.7.0 | Reblebro4200",creamInk,0.78f*s);
        renderer.text(x+22*s,y+62*s,"ENTER   Resume",menuInk,1.0f*s);
        renderer.text(x+22*s,y+97*s,"R       Return to camp",menuInk,1.0f*s);
        renderer.text(x+22*s,y+132*s,"T       Settings",menuInk,1.0f*s);
        renderer.text(x+22*s,y+167*s,keyName(platform::F11)+"      Fullscreen",menuInk,1.0f*s);
        renderer.text(x+22*s,y+202*s,"Q       Save and quit",menuInk,1.0f*s);
        if(!worldTitle.empty()){
            renderer.text(x+22*s,y+237*s,"W       Save and choose world",menuInk,1.0f*s);
            renderer.text(x+22*s,y+279*s,worldTitle,menuMuted,0.82f*s);
            if(world.seed)renderer.text(x+22*s,y+298*s,"Seed "+std::to_string(*world.seed),menuMuted,0.70f*s);
        }
    }
}

void Game::menuInput(int physical,int logical){
    using namespace platform;
    if(settingsPage || paused){key(physical);return;}
    const int close=inventory?Tab:crafting?C:materials?H:electrical?R:map?M:machineConsole?E:0;
    if(logical && (logical==close || (electrical && (logical==I || logical==M)))){key(logical);return;}
    if(electrical && (physical==I || physical==M) && bindings->physical(physical)!=physical)return;
    if(physical==close && bindings->physical(close)!=close)return;
    key(physical);
}

void Game::bindInput(int physical){
    if(!bindingCapture)return;
    if(physical==platform::Escape){bindingCapture=false;return;}
    if(!input::Bindings::assignable(physical))return;
    if(!bindings->assign(bindingRow,physical)){
        message(physical==platform::G?"G is reserved for loot rolls":"Choose a key or mouse button for a held action");return;
    }
    bindingCapture=false;
    if(!bindings->save())message("Could not save controls");
}

void Game::settingsClick(float mouseX,float mouseY,int width,int height){
    const float s=std::clamp(std::min(width/1280.0f,height/720.0f),0.63f,2.2f);
    if(!settingsPage && paused){
        const float x=(width-440*s)/2,y=(height-(worldTitle.empty()?263:335)*s)/2;
        if(mouseX>=x+18*s && mouseX<=x+422*s && mouseY>=y+128*s && mouseY<y+163*s)settingsPage=1;
        return;
    }
    if(settingsPage==1){
        const float x=(width-596*s)/2,y=(height-258*s)/2;
        if(mouseX>=x+17*s && mouseX<=x+579*s){
            if(mouseY>=y+132*s && mouseY<y+171*s)settingsPage=2;
            else if(mouseY>=y+174*s && mouseY<y+208*s)settingsPage=3;
        }
        return;
    }
    if(settingsPage==3){
        const float x=(width-680*s)/2,y=(height-570*s)/2;
        if(mouseX<x+17*s || mouseX>x+663*s || mouseY<y+61*s || mouseY>=y+(61+12*34)*s)return;
        const int first=std::clamp(graphicsRow-11,0,GraphicsOptions::Count-12);
        graphicsRow=std::clamp(first+int((mouseY-y-61*s)/(34*s)),0,GraphicsOptions::Count-1);
        if(mouseX>=x+445*s && mouseX<x+480*s)renderer.graphics().adjust(graphicsRow,-1);
        else if(mouseX>=x+610*s)renderer.graphics().adjust(graphicsRow,1);
        return;
    }
    if(settingsPage!=2)return;
    const float x=(width-596*s)/2,y=(height-550*s)/2;
    if(mouseX<x+15*s || mouseX>x+581*s || mouseY<y+62*s || mouseY>=y+62*s+14*28*s)return;
    const int first=std::clamp(bindingRow-6,0,int(input::actions.size())-14);
    bindingRow=std::min(int(input::actions.size())-1,first+int((mouseY-y-62*s)/(28*s)));
    if(input::actions[bindingRow].first!=platform::G)bindingCapture=true;
}

void Game::drawSettings(float s){
    const float w=(settingsPage==3?680:596)*s,h=(settingsPage==1?258:settingsPage==3?570:550)*s,x=(fieldWidth-w)/2,y=(fieldHeight-h)/2;
    menuFrame(renderer,x,y,w,h,s,settingsPage==1?"Settings":settingsPage==3?"Graphics":"Keybinds");
    if(settingsPage==3){
        constexpr std::array<const char*,GraphicsOptions::Count> names={"3D resolution / supersampling","Antialiasing","Bloom","Canopy shadows","God rays","Render distance","Microscopic texture detail","Foliage sprigs","VSync","FPS limit","Ambient occlusion","Shadow distance","Anisotropic filtering","Shadow filtering","Filmic tone mapping","Exposure (filmic)","Bloom threshold","Bloom quality"};
        const int first=std::clamp(graphicsRow-11,0,GraphicsOptions::Count-12);
        for(int i=first;i<first+12;i++){
            const float row=y+(61+(i-first)*34)*s;
            if(i==graphicsRow)renderer.rect(x+17*s,row,w-34*s,32*s,{.37f,.23f,.13f});
            renderer.text(x+25*s,row+5*s,names[i],i==graphicsRow?goldInk:menuInk,.83f*s);
            renderer.text(x+459*s,row+5*s,"<",creamInk,.9f*s);
            renderer.text(x+480*s,row+5*s,renderer.graphicsLabel(i),creamInk,.83f*s);
            renderer.text(x+635*s,row+5*s,">",creamInk,.9f*s);
        }
        renderer.text(x+25*s,y+476*s,"UP/DOWN select   LEFT/RIGHT change   ESC Back",menuMuted,.78f*s);
        renderer.text(x+25*s,y+503*s,"1 Laptop  2 Standard  3 High  4 Desktop Ultra",menuMuted,.78f*s);
        renderer.text(x+25*s,y+530*s,"BACKSPACE Restore defaults  |  "+std::to_string(graphicsRow+1)+" / "+std::to_string(GraphicsOptions::Count),menuMuted,.78f*s);
        return;
    }
    if(settingsPage==1){
        renderer.text(x+25*s,y+65*s,"Look speed     "+number(sensitivity*100,0)+"%     UP / DOWN",menuInk,0.95f*s);
        renderer.text(x+25*s,y+102*s,"3D resolution  "+std::to_string(renderer.renderScale())+"%     LEFT / RIGHT",menuInk,0.95f*s);
        renderer.rect(x+17*s,y+135*s,w-34*s,34*s,{0.37f,0.23f,0.13f});
        renderer.text(x+25*s,y+140*s,"Keybinds                        ENTER",creamInk,0.95f*s);
        renderer.rect(x+17*s,y+174*s,w-34*s,34*s,{0.37f,0.23f,0.13f});
        renderer.text(x+25*s,y+179*s,"Graphics                             G",creamInk,.95f*s);
    renderer.text(x+25*s,y+209*s,"ESC  Back",menuMuted,0.82f*s);
        return;
    }
    const int first=std::clamp(bindingRow-6,0,int(input::actions.size())-14);
    for(int i=first;i<std::min(int(input::actions.size()),first+14);i++){
        const float row=y+(62+(i-first)*28)*s;
        const bool selected=i==bindingRow;
        if(selected)renderer.rect(x+17*s,row,w-34*s,27*s,{0.37f,0.23f,0.13f});
        renderer.text(x+25*s,row+3*s,input::actions[i].second,selected?goldInk:menuInk,0.86f*s);
        renderer.text(x+367*s,row+3*s,selected && bindingCapture?"Press a key or button":keyName(input::actions[i].first),
            selected?creamInk:menuMuted,0.83f*s);
    }
    renderer.text(x+25*s,y+470*s,"Click an action or use UP/DOWN and ENTER",menuMuted,0.80f*s);
    renderer.text(x+25*s,y+501*s,"BACKSPACE  Reset defaults       ESC  Back",menuMuted,0.80f*s);
    if(statusTime>0 && (status=="Could not save controls" || status=="Choose a key or mouse button for a held action" || status=="G is reserved for loot rolls"))renderer.text(x+25*s,y+522*s,status,menuAccent,0.75f*s);
}

void Game::drawMachine(float s) const{
    const auto found=devices.find(*machineConsole);
    if(found==devices.end())return;
    const auto& machine=found->second;
    const bool quarry=machine.id==Quarry;
    const float w=485*s,h=258*s,x=(fieldWidth-w)/2,y=(fieldHeight-h)/2;
    const float left=x+22*s;
    menuFrame(renderer,x,y,w,h,s,quarry?"Quarry":"Resource Generator");
    renderer.text(left,y+53*s,std::string("Target  ")+materialName(machine.material),menuInk,0.92f*s);
    renderer.text(x+w-129*s,y+56*s,"LEFT / RIGHT",menuMuted,0.74f*s);
    double progress=0;
    std::string state;
    const char* action;
    if(quarry){
        const auto load=network.quarries.find(found->first);
        const double watts=load==network.quarries.end()?0:generator.rms2*load->second*load->second/quarryOhms;
        progress=machine.cycleEnergy>0?machine.energy/machine.cycleEnergy:0;
        if(!machine.closed)state="Paused";
        else if(generator.field==0 || watts<0.01)state="Needs generator power";
        else if(machine.probeDelay>0)state="Searching for a nearby deposit";
        else state="Mining";
        state+="   |   "+number(watts,2)+" W";
        action=machine.closed?"ENTER  Pause":"ENTER  Resume";
    }else{
        progress=machine.energy;
        state=machine.closed?"Producing one block per second":"Paused";
        action=machine.closed?"ENTER  Pause":"ENTER  Resume";
    }
    renderer.text(left,y+90*s,state,menuInk,0.86f*s);
    renderer.rect(left,y+125*s,w-44*s,6*s,rule);
    if(progress>0)renderer.rect(left,y+125*s,(w-44*s)*std::min(progress,1.0),6*s,mint);
    renderer.text(left,y+146*s,action,menuInk,0.84f*s);
    if(statusTime>0)paragraph(renderer,left,y+178*s,w-44*s,status,menuAccent,0.77f*s);
    renderer.text(left,y+220*s,keyName(platform::E)+" / ESC  Close",menuMuted,0.80f*s);
}

void Game::draw(int width,int height) {
    fieldWidth=std::max(1,width);fieldHeight=std::max(1,height);
    if(level>=0)drawMicro();
    else drawField();
    renderer.drawSky();
    drawWeapon();
    drawHud();
    renderer.finishHud();
}

namespace {
void writeTerrainEdit(std::ostream& out,const Edit& edit){
    out<<edit.level<<" "<<edit.position.x<<" "<<edit.position.y<<" "<<edit.position.z<<" "<<edit.id;
    if(edit.surface)for(auto h:*edit.surface)out<<" "<<h;
    out<<"\n";
}
bool readTerrainEdit(std::istream& entry,Edit& edit){
    if(!(entry>>edit.level>>edit.position.x>>edit.position.y>>edit.position.z>>edit.id) ||
       !terrainEditsAllowed(edit.id))return false;
    entry>>std::ws;
    if(!entry.eof()){
        SoilHeights heights{};
        if(!sculptable(edit.id))return false;
        for(auto& h:heights)if(!(entry>>h))return false;
        edit.surface=heights;
    }
    return true;
}
}
bool Game::save() const {
    if(saveFile.empty())return true;
    std::error_code error;
    const auto folder=saveFile.parent_path();
    if(!folder.empty())std::filesystem::create_directories(folder,error);
    if(error)return false;
    auto pending=saveFile;pending+=".tmp";
    std::ofstream out(pending);
    if(!out)return false;
    out<<"FRONTIER 7\n"<<std::setprecision(12);
    out<<"mode "<<modernWorld<<"\nroll "<<lootState<<" "<<rollCooldown<<"\n";
    const double savedYaw=level>=0?surfaceYaw:yaw;
    const double savedPitch=level>=0?surfacePitch:pitch;
    out<<"pos "<<feet.x<<" "<<feet.y<<" "<<feet.z<<" "<<savedYaw<<" "<<savedPitch<<" "<<zoom<<"\n";
    if(world.seed)out<<"seed "<<*world.seed<<"\n";
    out<<"rigmode "<<legacyRig<<"\n";
    out<<"state "<<built<<" 0 "<<wired<<" "<<crawled<<" "<<lesson<<" "<<night<<" "<<playTime<<"\n";
    out<<"solarclock "<<solarClock<<"\n";
    out<<"pickuses "<<pickUses<<"\n";
    out<<"witnesshealth "<<witnessHealth<<"\n";
    for(int i=0;i<2;i++)if(broods[i].slot>=0 || broods[i].clock>0)
        out<<"brood "<<i<<" "<<broods[i].slot<<" "<<broods[i].age<<" "<<broods[i].clock<<"\n";
    out<<"delbert "<<health<<" "<<battery<<" "<<armed<<" "<<(weapon==PulseRifle)<<" "<<weapon<<"\n";
    for(int i=0;i<int(guards.size());i++)if(guards[i].ready){
        const auto& guard=guards[i];
        out<<"goblin "<<i<<" "<<guard.feet.x<<" "<<guard.feet.y<<" "<<guard.feet.z<<" "<<
            guard.health<<" "<<guard.alert<<" "<<guard.looted<<" "<<guard.fallenToWitness<<"\n";
    }
    for(int i=0;i<int(gates.size());i++){
        out<<"gate "<<i<<" "<<bool(gates[i]);
        if(gates[i])out<<" "<<gates[i]->x<<" "<<gates[i]->y<<" "<<gates[i]->z;
        out<<"\n";
    }
    for(int set=0;set<2;set++)for(int depth=0;depth<micro::Total;depth++)
        if(fauna[set][depth] && fauna[set][depth]->defeated.any())
            out<<"fauna "<<set<<" "<<depth<<" "<<fauna[set][depth]->defeated.to_ulong()<<"\n";
    for(const auto& tile:consumedDeer)out<<"consumeddeer "<<tile.x<<" "<<tile.z<<"\n";
    if(rememberedVoice>=0)out<<"witnessvoice "<<rememberedVoice<<"\n";
    if(learnedVoices.any())out<<"witnessvoices "<<learnedVoices.to_ulong()<<"\n";
    for(int set=0;set<2;set++)for(int depth=0;depth<micro::Total;depth++)
        if(fauna[set][depth] && fauna[set][depth]->missingElves.any())
            out<<"missingelves "<<set<<" "<<depth<<" "<<fauna[set][depth]->missingElves.to_ulong()<<"\n";
    if(northHome && devices.contains(*northHome) && devices.at(*northHome).id==North)
        out<<"northfollow "<<northHome->x<<" "<<northHome->y<<" "<<northHome->z<<" "<<northFeet.x<<" "<<northFeet.y<<" "<<northFeet.z<<" "<<northHeading<<"\n";
    out<<"look "<<sensitivity<<"\n";
    out<<"hotbar "<<int(placement[selected])<<"\n";
    out<<"rig "<<generator.rpm<<" "<<generator.turns<<" "<<generator.field<<" "<<generator.test<<"\n";
    for(int i=0;i<ItemLimit;i++)if(bag[i])out<<"bag "<<i<<" "<<bag[i]<<"\n";
    for(const auto& [tile,edits]:world.saveChunks()){
        out<<"chunk "<<tile.x<<" "<<tile.z<<"\n";
        for(const auto& edit:edits){out<<"edit ";writeTerrainEdit(out,edit);}
    }
    for(const auto& [cell,device]:devices)out<<"dev "<<cell.x<<" "<<cell.y<<" "<<cell.z<<" "
        <<device.id<<" "<<device.closed<<" "<<device.normal.x<<" "<<device.normal.y<<" "<<device.normal.z<<" "<<device.material<<" "<<device.energy<<" "
        <<device.cycleEnergy<<" 0 0\n";
    for(const auto& [cell,seen]:visited)if(seen)out<<"seen "<<cell.x<<" "<<cell.z<<"\n";
    saveMicro(out);
    out<<"end\n";
    out.close();
    if(!out)return false;
    std::filesystem::rename(pending,saveFile,error);
    return !error;
}
void Game::load(){
    if(saveFile.empty())return;
    std::ifstream in(saveFile);
    if(!in){
        if(std::filesystem::exists(saveFile)){
            message("Save is unreadable. Original kept intact.");
            readOnly();
        }
        return;
    }
    std::string line;
    if(!std::getline(in,line) || (line!="FRONTIER 3" && line!="FRONTIER 4" && line!="FRONTIER 5" && line!="FRONTIER 6" && line!="FRONTIER 7")){
        message("Save format is unrecognized. Original kept intact.");
        readOnly();
        return;
    }
    surfaceFortress=false;
    std::size_t read=0;
    bool complete=false;
    while(std::getline(in,line) && read++<600000) {
        std::istringstream entry(line);
        std::string key;entry>>key;
        if(key=="end"){complete=true;break;}
        if(key=="mode"){
            if(entry>>modernWorld)world.sculptedTerrain=modernWorld;
        }else if(key=="roll"){
            double wait;
            if(entry>>lootState>>wait && std::isfinite(wait))rollCooldown=std::clamp(wait,0.0,2.0);
        }else if(key=="northfollow"){
            Cell home;Vec pos;double heading;
            if(entry>>home.x>>home.y>>home.z>>pos.x>>pos.y>>pos.z>>heading &&
                std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z) && std::isfinite(heading) &&
                std::abs(pos.x)<2048 && std::abs(pos.z)<2048 && pos.y>=0 && pos.y<ForestWorldHeight){
                northHome=home;northFeet=pos;northHeading=std::remainder(heading,2*Pi);
            }
        }else if(key=="pos"){
            Vec pos;double h,p,z;
            if(entry>>pos.x>>pos.y>>pos.z>>h>>p>>z && std::max(std::abs(pos.x),std::abs(pos.z))<2048 &&
                pos.y>=0 && pos.y<96 && std::isfinite(h) && std::isfinite(p) && std::isfinite(z)){
                feet=pos;yaw=std::remainder(h,2*Pi);pitch=std::clamp(p,-1.54,1.54);zoom=goalZoom=std::clamp(z,0.0,mobility::SurfaceZoom);
                precision=mobility::surfaceDetail(zoom);
            }
        }else if(key=="seed"){
            std::uint64_t value;
            if(entry>>value)world.setSeed(value);
        }else if(key=="rigmode"){
            entry>>legacyRig;
        }else if(key=="state"){
            int b,i,w,c,l,n;double t;
            if(entry>>b>>i>>w>>c>>l>>n>>t && std::isfinite(t)){
                built=b;wired=w&&b;crawled=c;lesson=std::clamp(l,0,7);night=n;playTime=std::max(0.0,t);
            }
        }else if(key=="solarclock"){
            double value;if(entry>>value && std::isfinite(value))solarClock=solarcycle::wrap(value);
        }else if(key=="delbert"){
            int hp,equipped,optic;double charge;
            if(entry>>hp>>charge>>equipped>>optic && std::isfinite(charge)){
                health=std::clamp(hp,1,100);battery=std::clamp(charge,0.0,expedition::BatteryCapacity);
                armed=equipped!=0;sightOn=optic!=0;
                int model;if(entry>>model && model==PulseRifle)weapon=PulseRifle;
            }
        }else if(key=="witnesshealth"){
            int hp;if(entry>>hp)witnessHealth=std::clamp(hp,0,WitnessMaxHealth);
        }else if(key=="brood"){
            int f,slot;double age,clock;
            if(entry>>f>>slot>>age>>clock && f>=0 && f<2 && slot>=-1 && slot<settlement::FortressGuards && std::isfinite(age) && std::isfinite(clock)){
                broods[f].slot=slot;broods[f].age=std::clamp(age,0.,settlement::BroodDuration);broods[f].clock=std::max(0.,clock);
            }
        }else if(key=="goblin"){
            int index,health,alert,looted;
            Vec point;
            if(entry>>index>>point.x>>point.y>>point.z>>health>>alert>>looted &&
                index>=0 && index<int(guards.size()) && std::abs(point.x)<2048 && std::abs(point.z)<2048 &&
                point.y>0 && point.y<95 && health>=0 && health<=110){
                auto& guard=guards[index];guard.ready=true;guard.feet=point;guard.health=health;
                guard.alert=alert!=0;guard.looted=looted!=0;int killed=0;if(entry>>killed)guard.fallenToWitness=killed!=0 && health==0;
                if(health==0)guard.visualClock=dread::FallDuration;
            }
        }else if(key=="gate"){
            int index,present;Vec anchor;
            if(entry>>index>>present && index>=0 && index<int(gates.size())){
                if(!present)gates[index].reset();
                else if(entry>>anchor.x>>anchor.y>>anchor.z && anchor.y>=1 && anchor.y<world.heightLimit() &&
                    std::abs(anchor.x)<2048 && std::abs(anchor.z)<2048)gates[index]=anchor;
            }
        }else if(key=="consumeddeer"){
            int x,z;if(entry>>x>>z && x>=-64 && x<64 && z>=-64 && z<64){
                consumedDeer.insert({x,0,z});forestDeer.erase({x,0,z});
            }
        }else if(key=="witnessvoice"){
            int voice;if(entry>>voice && voice>=0 && voice<=5){rememberedVoice=voice;learnedVoices.set(voice);}
        }else if(key=="witnessvoices"){
            unsigned long bits;if(entry>>bits && bits<64)learnedVoices|=std::bitset<6>(bits);
        }else if(key=="missingelves"){
            int set,depth;unsigned long bits;
            // Only one nonessential villager can be lost in each village.
            if(entry>>set>>depth>>bits && set>=0 && set<2 && depth==expedition::Village &&
                (bits==2 || bits==4 || bits==8)){
                auto& encounter=fauna[set][depth];
                if(!encounter)encounter=std::make_unique<expedition::Wildlife>();
                encounter->missingElves=std::bitset<4>(bits);
            }
        }else if(key=="fauna"){
            int set,depth;unsigned long bits;
            if(entry>>set>>depth>>bits && set>=0 && set<2 && depth>=0 && depth<micro::Total){
                auto& encounter=fauna[set][depth];
                if(!encounter)encounter=std::make_unique<expedition::Wildlife>();
                encounter->defeated=std::bitset<expedition::Hostiles>(bits);
            }
        }else if(key=="flight"){
            int value;if(entry>>value)flying=value!=0;
        }else if(key=="micro"){
            int room,depth;Vec p;double h,t,z;
            if(entry>>room>>depth>>p.x>>p.y>>p.z>>h>>t>>z && room>=0 && room<2 &&
                depth>=0 && depth<micro::Total && p.x>=0 && p.x<micro::Width &&
                p.y>=0 && p.y<micro::Height && p.z>=0 && p.z<micro::Depth &&
                std::isfinite(h) && std::isfinite(t) && std::isfinite(z)){
                surfaceYaw=yaw;surfacePitch=pitch;
                family=room;level=depth;innerFeet=p;yaw=h;pitch=std::clamp(t,-1.54,1.54);
                innerZoom=innerGoal=std::clamp(z,0.0,mobility::RealmZoom);
                layer(room,depth);
            }
        }else if(key=="surfacefortress"){
            int active=0;
            if(entry>>active && active==1 && inFortress()){
                surfaceFortress=true;zoom=goalZoom=FineDepth;innerZoom=innerGoal=0;
            }
        }else if(key=="rope"){
            int depth;DepthPoint point;
            if(entry>>depth>>point.feet.x>>point.feet.y>>point.feet.z>>point.yaw>>point.pitch>>point.zoom &&
                depth>=0 && depth<micro::Count && point.feet.x>=0 && point.feet.x<micro::Width &&
                point.feet.y>=0 && point.feet.y<micro::Height && point.feet.z>=0 && point.feet.z<micro::Depth &&
                std::isfinite(point.pitch) && std::isfinite(point.yaw) && std::isfinite(point.zoom))rope[depth]=point;
        }else if(key=="tiny"){
            int room,depth,id;Cell cell;
            if(entry>>room>>depth>>cell.x>>cell.y>>cell.z>>id && room>=0 && room<2 && depth>=0 && depth<micro::Total &&
                micro::within(cell) && (terrainEditsAllowed(id) || furniture(id)))layer(room,depth).set(cell,id);
        }else if(key=="tinyfine"){
            int room,depth;
            Edit edit{};
            if(entry>>room>>depth && room>=0 && room<2 && depth>=0 && depth<micro::Total &&
                readTerrainEdit(entry,edit) && edit.level>0 && edit.level<=FineDepth)
                layer(room,depth).setDetail(edit.level,edit.position,edit.id,true,edit.surface);
        }else if(key=="hotbar"){
            int id;
            if(entry>>id){
                const auto found=std::find(placement.begin(),placement.end(),id);
                if(found!=placement.end())selected=int(found-placement.begin());
            }
        }else if(key=="pickuses"){
            int remaining;
            if(entry>>remaining)pickUses=std::clamp(remaining,1,96);
        }else if(key=="look"){
            double value;
            if(entry>>value && std::isfinite(value))sensitivity=std::clamp(value,0.4,2.6);
        }else if(key=="rig"){
            double speed,field;int turns,test;
            if(entry>>speed>>turns>>field>>test && std::isfinite(speed)){
                generator.rpm=std::clamp(speed,0.0,240.0);generator.turns=std::clamp(turns,1,80);
                generator.field=field==0.32?0.32:field==0?0:0.08;generator.test=test;
            }
        }else if(key=="bag") {
            int id;std::int64_t units;
            if(entry>>id>>units && id>0 && id<ItemLimit && units>=0 && units<std::int64_t(FullBlock)*1000000)bag[id]=(units/FullBlock)*FullBlock;
        }else if(key=="edit") {
            Edit edit;
            if(readTerrainEdit(entry,edit))world.apply(edit);
        }else if(key=="dev"){
            Cell c,n;int id,closed;
            if(entry>>c.x>>c.y>>c.z>>id>>closed>>n.x>>n.y>>n.z &&
                c.x>=-2048 && c.x<=2047 && c.z>=-2048 && c.z<=2047 && c.y>=1 && c.y<world.heightLimit() &&
                (!legacyRig || c!=Port) && mounted(id) && std::find(Sides.begin(),Sides.end(),n)!=Sides.end()){
                Device placed{id,closed!=0,n};
                int ore;double energy;
                if(entry>>ore>>energy && std::find(materialsList.begin(),materialsList.end(),ore)!=materialsList.end() && std::isfinite(energy)){
                    placed.material=ore;placed.energy=std::clamp(energy,0.0,1000000.0);
                }
                double budget,remaining;
                if(entry>>budget>>remaining && std::isfinite(budget) && std::isfinite(remaining)){
                    if(id==Quarry)placed.cycleEnergy=std::clamp(budget,0.0,1000000.0);

                }
                devices.emplace(c,placed);
            }
        }else if(key=="seen"){
            int x,z;
            if(entry>>x>>z && x>=-64 && x<64 && z>=-64 && z<64)visited.emplace(Cell{x,0,z},true);
        }
    }
    
    std::optional<Cell> north;
    for(const auto& [cell,device]:devices)if(device.id==North &&
        (!north || std::tie(cell.x,cell.y,cell.z)<std::tie(north->x,north->y,north->z)))north=cell;
    if(north){
        std::erase_if(devices,[&](const auto& item){return item.second.id==North && item.first!=*north;});
        bag[North]=0;
    }else bag[North]=std::min<std::int64_t>(bag[North],FullBlock);
    if(!bag[weapon])weapon=bag[PulseRifle]?PulseRifle:PulsePistol;
    armed=armed && bag[weapon]>=FullBlock;
    sightOn=armed && weapon==PulseRifle; // ignore old detachable-optic save flag
    if(!complete){
        message("Recovered part of an incomplete save. Original kept intact.");
        readOnly();
    }
}
void Game::capture(std::filesystem::path p){renderer.screenshot(p);}
void Game::smoke(std::filesystem::path prefix){
    auto show=[&](const std::string& name){
        for(int i=0;i<15;i++)draw(1280,720);
        capture(prefix.string()+"-"+name+".ppm");
    };
    show("camp");
    paused=true;show("pause");paused=false;
    crafting=true;show("workbench");crafting=false;
    built=true;feet={0.5,29,15.1};yaw=0;pitch=-0.16;
    devices.emplace(Cell{0,29,12},Device{Cable,true,{0,1,0}});
    devices.emplace(Cell{0,29,13},Device{Lamp,true,{0,1,0}});
    refreshGrid();
    for(int i=0;i<120;i++)update(1.0/60,{});
    night=true;show("circuit");night=false;
    const Cell quarryMachine{1,29,11},north{-1,29,12};
    devices.emplace(quarryMachine,Device{Quarry,true,{0,1,0},IronOre});
    devices.emplace(north,Device{North,true,{0,1,0},Quartz});
    devices.emplace(Cell{-1,29,13},Device{Fabricator});
    refreshGrid();
    for(int i=0;i<120;i++)update(1.0/60,{});
    show("workshop");
    machineConsole=quarryMachine;show("quarry-controls");
    machineConsole.reset();
    yaw=std::atan2(north.x+0.5-feet.x,-(north.z+0.5-feet.z));pitch=-0.02;
    show("north");
    yaw=0;pitch=0.21;night=true;show("night-sky");night=false;
    paused=true;settingsPage=2;show("keybinds");settingsPage=0;paused=false;
    feet={55.8,17.0,23.6};yaw=Pi/2;pitch=-0.17;goalZoom=zoom=0.95;precision=1;
    world.stream(floorCell(feet),2);
    show("quarry");
    feet={63.2,17.0,23.6};yaw=Pi/2;pitch=0;goalZoom=zoom=1.10;precision=1;
    world.stream(floorCell(feet),2);
    show("tunnel");
    feet={74.8,17.0,23.8};yaw=Pi/2;pitch=0.22;goalZoom=zoom=3.7;precision=4;
    world.stream(floorCell(feet),2);
    show("tiny");
    // Draw a stable camera position at both player sizes to catch geometry that
    // still incorrectly expands with the selected render level.
    const Cell fixtureCube{27,59,-11};
    world.stream(fixtureCube,2);
    world.apply({0,fixtureCube,Stone});
    const int samples=subdivisions(2);
    for(int y=7;y<=9;y++)for(int z=6;z<=8;z++)world.apply({2,fixtureCube*samples+Cell{0,y,z},Air});
    const auto chip=Cell{26,59,-11}*samples+Cell{1,8,10};
    world.apply({2,chip,Timber});
    const Vec testCamera{24.7,59.55,-10.50};
    const Vec testView={27,59.53,-10.5};
    const Vec delta=testView-testCamera;
    yaw=std::atan2(delta.x,-delta.z);pitch=std::atan2(delta.y,std::hypot(delta.x,delta.z));
    statusTime=0;zoom=goalZoom=0;precision=0;
    feet=testCamera-Vec{0,EyeHeight*body(),0};target.reset();deviceTarget.reset();
    show("edit-grown");
    zoom=goalZoom=1.7;precision=2;feet=testCamera-Vec{0,EyeHeight*body(),0};
    show("edit-shrunk");
    family=0;
    for(int n=0;n<micro::Count;n++){
        level=n;innerZoom=0;innerFeet={24.5,5.03,19.8};yaw=2.68;pitch=-0.14;
        if(level==6){innerFeet={28.5,7.01,21.89};yaw=2.24;pitch=0.06;}
        if(level==expedition::Village){innerFeet={24.5,5.01,17};yaw=Pi-0.55;pitch=0.02;}
        focus();show("hidden-"+std::to_string(n));
    }
    level=expedition::Village;innerFeet={26.9,5.01,12.5};yaw=1.84;pitch=-0.03;
    auto& elf=wildlife(family,level).elves[1];
    elf.feet={28.7,5.01,13.02};elf.yaw=-1.8;
    focus();show("elderbough-elf");
    bag[PulsePistol]=bag[PulseRifle]=bag[PowerPack]=FullBlock;
    bag[PulseRounds]=12LL*FullBlock;battery=1000;armed=true;sightOn=false;
    weapon=PulsePistol;show("pulse-pistol");
    weapon=PulseRifle;sightOn=true;show("quartz-sight");
    fire();
    updateExpedition(0.025);
    show("pulse-round");

    armed=sightOn=false;
    for(auto& shot:bolts)shot.age=1;
    innerFeet={24.53,5.004,7.75};innerZoom=innerGoal=mobility::RealmZoom;
    level=micro::Count-1;yaw=Pi/2;pitch=-0.31;
    const int resolution=subdivisions(4);
    const auto grain=floorCell(camera()*resolution);
    for(int i=3;i<=9;i+=2){
        const int id=i%3?Quartz:CopperOre;
        layer(family,level).setDetail(4,grain+Cell{i,-1,i%3-1},id);
    }
    focus();show("smallest-realm");

    family=level=-1;feet={21.52,59.0000001,-13.25};
    zoom=goalZoom=mobility::SurfaceZoom;precision=FineDepth;
    world.stream(floorCell(feet),2);
    world.apply({0,{21,58,-14},Stone});
    const int lattice=subdivisions(FineDepth);
    const auto seed=floorCell(camera()*lattice);
    for(int i=1;i<=6;i++){
        const int id=i%3?IronOre:Quartz;
        world.apply({FineDepth,seed+Cell{i,0,i%3-1},id});
    }
    focus();show("smallest-surface");
    feet={22.5,61.0002,-19.5};yaw=0;pitch=-0.095;precision=0;zoom=goalZoom=0;
    for(int z=-28;z<=-18;z++)for(int x=18;x<=26;x++)world.apply({0,{x,60,z},Stone});
    world.apply({0,{21,61,-24},IronOre});
    world.apply({0,{23,61,-25},Lodestone});
    world.apply({0,{22,61,-22},Quartz});
    world.apply({0,{25,61,-27},CopperOre});
    focus();show("outcrop");
    // Clean frames for the Steam library artwork.
    const auto scene=[&](const std::string& name){
        if(witness.active && level==witness.level && family==witness.family)updateWitnessRig(0);
        const bool large=name=="art-witness" || name=="witness-close" || name=="witness-climb";
        fieldWidth=large?2560:1280;fieldHeight=large?1440:720;target.reset();deviceTarget.reset();gateTarget.reset();
        for(int frame=0;frame<6;frame++){
            if(level<0)drawField();else drawMicro();
            renderer.drawSky();
            renderer.finishHud();
        }
        capture(prefix.string()+"-"+name+".ppm");
    };
    feet={20.00,33.5,-1.5};yaw=std::atan2(-7.5,5.0);pitch=-0.23;
    zoom=goalZoom=0;precision=0;
    world.stream(floorCell(feet),2);night=true;scene("art-surface");
    yaw-=0.15;night=false;scene("art-day");
    family=0;level=expedition::Village;innerZoom=0;
    innerFeet={24.6,5.08,18.0};yaw=Pi-0.48;pitch=0.09;
    scene("art-elderbough");
    innerFeet={26.9,5.02,12.5};yaw=1.84;pitch=0.13;
    scene("art-elf");
    family=level=-1;feet={north.x+0.44,29,north.z+2.48};yaw=0.02;pitch=0.02;
    scene("art-north");
    const auto footing=[&](double x,double z){
        const auto hit=world.ray({x,63,z},{0,-1,0},0,62);
        return Vec{x,hit?63-hit->distance+0.01:30,z};
    };
    feet=footing(17.5,-4);yaw=Pi/2;pitch=0.13;
    night=true;flashlight=true;
    witness={};witness.active=true;witness.family=family;witness.level=level;
    witness.feet=footing(29.5,-4);witness.heading=-Pi/2;
    pitch=std::atan2(witness.feet.y+2.7-camera().y,12.0);
    scene("art-witness");
    feet=footing(23.0,-4);
    pitch=std::atan2(witness.feet.y+3.40-camera().y,6.5);
    witness.observed=true;witness.stareTime=0.65;witness.sight.observe(camera(),0,witness.scale);
    scene("witness-close");
    for(int z=-6;z<=-2;z++)for(int y=29;y<=38;y++)world.apply({0,{31,y,z},Stone});
    witness.feet={30.003,32.0,-4};witness.climbing=witness.crouched=true;witness.heading=Pi/2;
    witness.climbDirection={1,0,0};witness.rig.ready=false;
    pitch=std::atan2(witness.feet.y+1.40-camera().y,7.0);
    scene("witness-climb");

}

void Game::previewRealms(const std::filesystem::path& path){
    readOnly();family=0;innerZoom=innerGoal=0;armed=false;flashlight=false;witness.active=false;
    // Sample two portal approaches and the full route through all realms.
    for(level=0;level<micro::Count;level++){
        innerFeet={24.5,5.01,8.5};yaw=Pi+.23;pitch=.04;target.reset();gateTarget.reset();
        if(layer(family,level).overlaps(innerFeet,innerSize()))throw std::runtime_error("Realm preview spawn obstructed");
        for(int i=0;i<7;i++)draw(960,540);
        capture(path.string()+"-"+std::to_string(level)+".ppm");
        if(level==0){
            innerFeet={24.5,5.01,29.5};yaw=Pi-1.05;pitch=.18;
            for(int i=0;i<7;i++)draw(960,540);
            capture(path.string()+"-canopy.ppm");
        }
        std::cout<<"Rendered hidden realm "<<level<<": "<<micro::details(level).name<<std::endl;
    }
    level=family=-1;
}

void Game::previewModern(const std::filesystem::path& path){
    const auto image=[&](std::string_view name,int width=1280,int height=720){
        for(int i=0;i<25;i++)draw(width,height);
        capture(path.string()+"-"+std::string(name)+".ppm");
    };
    image("start");
    feet={11.8,29.003,2.7};yaw=1.86;pitch=-0.14;
    world.stream(floorCell(feet),2);
    image("reedwatch");
    feet={17.0,30.7,7.1};yaw=-0.37;pitch=-0.24;
    image("reedwatch-roof");
    level=6;family=0;innerFeet={33.5,2.01,16.5};innerZoom=innerGoal=0;
    yaw=2.89;pitch=0.015;flashlight=true;
    image("hold");
    level=family=-1;flashlight=false;
    feet={300.6,50.8,304.7};yaw=0.555;pitch=0.43;
    world.stream(floorCell(feet),3);
    solarClock=solarcycle::CycleSeconds*.5;night=false;image("planet-day",2560,1440);
    solarClock=0;night=true;image("planet-night",2560,1440);
    solarClock=solarcycle::InitialClock;night=false;pitch=-0.37;image("contours");
    feet={-0.8,29.001,3.8};yaw=Pi;pitch=0.22;
    world.stream(floorCell(feet),2);
    witness={};
    witness.active=true;witness.family=family;witness.level=level;
    witness.feet={-1.1,29.001,10.4};witness.heading=Pi;
    witness.observed=true;witness.stareTime=0.78;witness.sight.observe(camera(),0,witness.scale);
    updateWitnessRig(0);
    pitch=std::atan2(witness.feet.y+2.7-camera().y,6.6);
    solarClock=0;night=true;flashlight=true;
    image("witness",1920,1080);
}

void Game::previewNorth(const std::filesystem::path& path){
    const auto seed=world.seed.value();
    double z=44;
    while(z<100 && (!natural::trailLand(0,z,seed)||!natural::trailLand(0,z+4,seed)))z+=2;
    const auto onTrail=[&](double depth){double x=natural::trailX(0,depth,seed);return Vec{x,natural::surfaceHeight(x,depth,seed)+.015,depth};};
    feet=onTrail(z);world.stream(floorCell(feet),2);
    const auto ground=[&](Vec p){if(const auto* r=world.roof(floorCell(p)))p.y=r->height(p.x-std::floor(p.x),p.z-std::floor(p.z))+.015;return p;};
    feet=ground(feet);family=level=-1;witness.active=false;
    std::erase_if(devices,[](const auto& p){return p.second.id==North;});
    const Vec north=ground(onTrail(z+3.3)+Vec{-1.15,0,0});
    const Cell cell=floorCell(north+Vec{0,.15,0});devices[cell]=Device{North};
    gates[0]=ground(onTrail(z+5.6)+Vec{1.6,0,0});gates[1].reset();
    const Vec target=(north+*gates[0])*.5+Vec{0,1.1,0}-camera();
    yaw=std::atan2(target.x,-target.z);pitch=std::atan2(target.y,std::hypot(target.x,target.z));
    auto shot=[&](const char* name){for(int i=0;i<9;i++)draw(1280,720);capture(path.string()+"-"+name+".ppm");};
    solarClock=solarcycle::CycleSeconds*.40;night=false;playTime=2;shot("day");
    playTime=4.89;shot("twitch");
    solarClock=0;night=true;playTime=6;shot("night");
    // Exercise the actual gameplay follower on generated terrain (no forced position after spawn).
    northHome.reset();northMoving=false;northWalk=northStride=0;
    const Vec before=northOrigin(cell);
    feet=ground(onTrail(z+12));world.stream(floorCell(feet),2);
    double start=length(Vec{feet.x-before.x,0,feet.z-before.z});
    for(int i=0;i<160;i++)updateNorth(.05);
    if(!northHome || length(Vec{northFeet.x-before.x,0,northFeet.z-before.z})<.4 ||
        length(Vec{feet.x-northFeet.x,0,feet.z-northFeet.z})>=start-.4 || overlapsField(northFeet,1.6,1.46))
        throw std::runtime_error("North failed generated-terrain follow test");
    std::cout<<"North followed across terrain: "<<start<<" -> "<<length(Vec{feet.x-northFeet.x,0,feet.z-northFeet.z})<<" meters\n";
    std::cout<<"Rendered North and an anchored portal in a generated world.\n";
}

void Game::previewGoblin(const std::filesystem::path& path){
    if(!modernWorld || !world.seed)throw std::runtime_error("Goblin preview requires a modern world");
    const auto seed=*world.seed;
    double z=44;
    while(z<100 && (!natural::trailLand(0,z,seed) || !natural::trailLand(0,z+4,seed)))z+=2;
    const auto onTrail=[&](double depth){double x=natural::trailX(0,depth,seed);return Vec{x,natural::surfaceHeight(x,depth,seed)+.015,depth};};
    feet=onTrail(z);world.stream(floorCell(feet),2);
    auto ground=[&](Vec p){if(const auto* r=world.roof(floorCell(p)))p.y=r->height(p.x-std::floor(p.x),p.z-std::floor(p.z))+.015;return p;};
    feet=ground(feet);family=level=-1;witness.active=false;
    auto& guard=goblin();guard.feet=ground(onTrail(z+2.6));
    guard.yaw=std::atan2(feet.x-guard.feet.x,feet.z-guard.feet.z)+.25;
    solarClock=solarcycle::CycleSeconds*.40;night=false;flashlight=false;
    auto capturePose=[&](const char* label){const Vec target=guard.feet+Vec{0,.80,0}-camera();
        yaw=std::atan2(target.x,-target.z);pitch=std::atan2(target.y,std::hypot(target.x,target.z));
        for(int frame=0;frame<8;frame++)draw(1280,720);
        capture(path.string()+"-"+label+".ppm");};
    capturePose("idle");
    feet=ground(onTrail(z+1.10));flashlight=true;capturePose("close");
    feet=ground(onTrail(z));flashlight=false;
    guard.visualClock=.5;guard.stride=Pi*.5;guard.visualMotion=1.;
    capturePose("moving");
    // Verify existing guard hit detection and chest state remain intact.
    Vec direction=unit(guard.feet+Vec{0,.9,0}-camera());
    if(goblinHit(camera(),direction,8)>=8 || guard.health!=110 || guard.looted)
        throw std::runtime_error("Goblin rendering changed the guard's gameplay state");
    std::cout<<"Articulated goblin rendered in the actual surface world; guard hit and chest state intact.\n";
}

void Game::previewWitness(const std::filesystem::path& path){
    const auto seed=world.seed.value();
    double z=44;
    while(z<100 && (!natural::trailLand(0,z,seed) || !natural::trailLand(0,z+4,seed)))z+=2;
    const auto onTrail=[&](double depth){
        const double x=natural::trailX(0,depth,seed);
        return Vec{x,natural::surfaceHeight(x,depth,seed)+.015,depth};
    };
    feet=onTrail(z);
    world.stream(floorCell(feet),2);
    const auto ground=[&](Vec p){
        if(const auto* r=world.roof(floorCell(p)))
            p.y=r->height(p.x-std::floor(p.x),p.z-std::floor(p.z))+.015;
        return p;
    };
    feet=ground(feet);
    witness={};witness.active=true;witness.crouched=true;witness.observed=true;
    witness.family=family;witness.level=level;witness.scale=1;
    witness.feet=ground(onTrail(z+3.1));
    const Vec delta=camera()-witness.feet;
    witness.heading=std::atan2(delta.x,delta.z)+.72;
    witness.sight.observe(camera(),0,witness.scale);
    for(int i=0;i<30;i++)updateWitnessRig(1.0/60);
    const Vec target=witness.feet+Vec{0,1.17,0}-camera();
    yaw=std::atan2(target.x,-target.z);pitch=std::atan2(target.y,std::hypot(target.x,target.z));
    solarClock=solarcycle::CycleSeconds*.40;night=false;flashlight=true;
    for(int i=0;i<15;i++)draw(1280,720);
    capture(path);

    // Exercise the same sight query used during pursuit. The preview runs in
    // a temporary world. Build an opaque screen between the two eye points.
    moveWitness(1./60);
    if(witness.sight.age!=0)throw std::runtime_error("Witness missed the player on the open trail");
    const Vec sighting=witness.sight.position;
    const Vec head=witness.rig.frame.point(witness.rig.pose.neck);
    const Cell mid=floorCell((head+sighting)*.5);
    const int axis=std::abs(head.x-sighting.x)>std::abs(head.z-sighting.z)?0:2;
    for(int y=-2;y<=2;y++)for(int k=-2;k<=2;k++){
        Cell wall=mid;wall.y+=y;wall[2-axis]+=k;
        world.apply({0,wall,Stone});
    }
    feet[2-axis]+=.3;
    moveWitness(1./60);
    if(witness.sight.age<=0 || witness.sight.position!=sighting)
        throw std::runtime_error("Witness tracked the player through cover");
    std::cout<<"Verified pursuit acquires the visible player and retains the last sighting behind cover.\n";
}

void Game::previewGraphics(const std::filesystem::path& path){
    // Hidden-mode integration test. The actual generated world and its real
    // renderer exercise the menu, not a mock UI or a second rendering engine.
    using namespace platform;
    const auto previous=renderer.graphics();
    const auto image=[&](const char* name,int frames){
        for(int i=0;i<frames;++i)draw(960,540);
        capture(path.string()+"-"+name+".ppm");
    };
    if(!modernWorld || !world.seed)throw std::runtime_error("Graphics preview requires a modern world");
    const auto seed=*world.seed;
    double z=44;
    while(z<100 && (!natural::trailLand(0,z,seed) || !natural::trailLand(0,z+12,seed)))z+=2;
    if(z>=100)throw std::runtime_error("Graphics preview: no suitable trail");
    const double x=natural::trailX(0,z,seed);
    world.stream(floorCell({x,30,z}),2);
    feet={x,natural::surfaceHeight(x,z,seed)+.01,z};
    if(const auto* roof=world.roof(floorCell(feet)))feet.y=roof->height(x-std::floor(x),z-std::floor(z))+.01;
    yaw=std::atan2(natural::trailX(0,z+18,seed)-x,-18.0);pitch=-.12;
    solarClock=solarcycle::CycleSeconds*.5;night=false;witness.active=false;
    renderer.graphics().preset(1);image("trail-day",18);
    // Real view-model integration, including a fresh rifle with no crafted sight.
    armed=false;weapon=PulseRifle;bag[PulseRifle]=FullBlock;bag[QuartzSight]=0;sightOn=false;
    if(visibleRecipe(19))throw std::runtime_error("Retired optic recipe is still visible");
    key(X);if(!armed || weapon!=PulseRifle || !sightOn)throw std::runtime_error("Starter rifle lacks permanent optic");
    key(Z);if(!sightOn)throw std::runtime_error("Integrated optic can be removed");
    // Rendering is independent even of the legacy attachment flag.
    sightOn=false;aiming=false;image("rifle-day",2);aiming=true;image("rifle-aim",2);
    aiming=false;armed=false;
    renderer.graphics().preset(3);image("trail-ultra",3);
    solarClock=0;night=true;image("trail-night",3);
    armed=true;image("rifle-night",2);armed=false;
    // Exercise the real menu, including keyboard presets, mouse row selection,
    // navigation and returning to the parent settings page.
    key(Escape);key(T);key(G);
    if(!paused || settingsPage!=3)throw std::runtime_error("Graphics tab failed to open");
    key(One);if(renderer.graphics().resolution!=70)throw std::runtime_error("Low preset did not apply");
    graphicsRow=0;key(Right);if(renderer.graphics().resolution!=80)throw std::runtime_error("Resolution adjustment failed");
    const float s=.75f,xMenu=(960-680*s)/2,yMenu=(540-570*s)/2;
    settingsClick(xMenu+637*s,yMenu+(61+2*34+8)*s,960,540);
    if(graphicsRow!=2 || renderer.graphics().bloom!=1)throw std::runtime_error("Graphics mouse adjustment failed");
    key(Four);image("graphics-menu",2);
    key(Escape);if(settingsPage!=1)throw std::runtime_error("Graphics back navigation failed");
    key(Escape);key(Escape);if(settingsPage || paused)throw std::runtime_error("Graphics menu did not close");
    // Extra terrain regression views at the default procedural seed. This
    // exercises the actual high-altitude mesh, shadow pass and grounded trail.
    if(seed==62108432){
        renderer.graphics().preset(1);
        solarClock=solarcycle::CycleSeconds*.5;night=false;witness.active=false;
        const double highZ=76,highX=natural::trailX(1,highZ,seed);
        feet={highX,natural::surfaceHeight(highX,highZ,seed)+.02,highZ};
        world.stream(floorCell(feet),3);
        if(const auto* roof=world.roof(floorCell(feet)))
            feet.y=roof->height(highX-std::floor(highX),highZ-std::floor(highZ))+.02;
        yaw=std::atan2(natural::trailX(1,highZ+18,seed)-highX,-18.0);pitch=.09;
        image("highland-trail",25);
        feet={190,91,147};yaw=std::atan2(139.0-feet.x,feet.z-99.0);pitch=-.48;
        world.stream(floorCell(feet),3);image("highland-aerial",25);
    }
    renderer.graphics()=previous;
    std::cout<<"PASS: generated trail, high quality day/night rendering and actual graphics menu controls\n";
}

void Game::verifyModern(const std::filesystem::path& data){
    using namespace platform;
    auto check=[](bool condition,const char* name){if(!condition)throw std::runtime_error(std::string("v0.7.0 test: ")+name);};
    check(modernWorld && world.seed && bag[PulseRifle]==FullBlock &&
        devices.at({-2,29,-4}).id==North,"starter rifle, North and procedural seed");
    check(!bag[QuartzSight] && !visibleRecipe(19),"starter does not require a separate sight");
    key(X);check(armed && weapon==PulseRifle && sightOn,"starter rifle has an integrated sight");
    key(Z);check(sightOn,"permanent sight cannot be removed");
    key(X);check(!armed,"rifle can still be holstered");
    flying=true;
    wheel(0.5);movement(1.0/60,{});
    check(precision==0,"partial scroll waits for a complete detent");
    wheel(0.5);movement(1.0/60,{});
    check(precision==1 && body()==0.25,"one complete scroll selects the first discrete tier");
    wheel(20);movement(1.0/60,{});
    check(precision==FineDepth && body()*subdivisions(FineDepth)==1 &&
        World::playerBox({},body()).hi.y*subdivisions(FineDepth)==1.5,
        "Delbert's standing collider is one and a half blocks at the deepest tier");
    check(inFortress() && surfaceFortress,"Ordinary shrinking opens the final-tier fortress");
    const double scaled=mobility::scaledSpeed(body(),false,true,false,true);
    const double walk=mobility::scaledSpeed(body(),false,true,false,false);
    check(scaled/walk<1.9 && scaled/body()<11,"tiny sprint has no absolute speed boost");
    wheel(-20);movement(1.0/60,{});
    check(precision==0 && body()==1,"wheel returns to the normal tier");
    flying=false;
    const auto previous=lootState;
    key(G);check(lootState!=previous && rollCooldown>0,"G rolls a reward");
    const auto rolled=lootState;
    key(G);check(lootState==rolled,"roll cooldown is respected");

    bool grassChecked=false;
    for(int z=-13;z<=16 && !grassChecked;z++)for(int x=-18;x<=18 && !grassChecked;x++){
        const auto* grass=world.roof({x,0,z});
        if(!grass)continue;
        const int y=int(std::ceil(grass->high()))-1;
        if(world.sample(0,{x,y,z}).id!=Grass)continue;
        const double height=grass->height(0.31,0.58);
        const Vec point{x+0.31,height,z+0.58};
        const auto pick=world.ray(point+Vec{0,0.35,0},{0,-1,0},0,0.70);
        auto step=world.sweep(point+Vec{0,0.10,0},0.001,1,-0.20);
        check(pick && pick->id==Grass && std::abs(pick->distance-0.35)<1e-7 &&
            std::abs(step+0.10)<0.001 && !world.overlaps(point+Vec{0,0.01,0},0.001),
            "starter grass uses its rendered height for picking and movement");
        grassChecked=true;
    }
    check(grassChecked,"the starter area has a contoured grass test surface");

    world.stream({320,45,319},2);
    const Cell column{319,0,319};
    const auto* profile=world.roof(column);
    const auto* next=world.roof({320,0,319});
    check(profile && next && std::abs(profile->height(1,0.37)-next->height(0,0.37))<1e-9,
         "continuous terrain shares boundary vertices across chunks");
    const double h=profile->height(0.38,0.28);
    const Vec spot{319.38,h,319.28};
    const auto ray=world.ray(spot+Vec{0,0.42,0},{0,-1,0},0,0.84);
    check(ray && std::abs(ray->distance-0.42)<2e-7,"mining ray reaches the rendered slope");
    check(world.overlaps(spot-Vec{0,0.05,0},0.01) && !world.overlaps(spot+Vec{0,0.05,0},0.01),
        "the movement hull agrees with the visible slope");
    const auto floor=world.sweep(spot+Vec{0,0.45,0},0.006,1,-0.9);
    check(floor<-.44 && floor>-.46,"downward collision lands on the slope");
    std::optional<Hit> detail;
    const double cellSize=1.0/subdivisions(FineDepth);
    detail=world.ray(spot+Vec{0,cellSize*2,0},{0,-1,0},FineDepth,cellSize*4);
    check(detail && std::abs(detail->distance-cellSize*2)<1e-7,"a fine ray resolves the same slope");
    check(world.apply({detail->level,detail->cell,Air})<0 &&
        !world.sample(detail->level,detail->cell).amount,"small edits remove the targeted solid cell");
    const Vec adjacent{319.58,profile->height(0.58,0.28),319.28};
    const auto intact=world.ray(adjacent+Vec{0,0.42,0},{0,-1,0},0,0.84);
    check(intact && std::abs(intact->distance-0.42)<2e-7 &&
        !world.overlaps(adjacent+Vec{0,0.05,0},0.01),"a microscopic cut leaves the surrounding slope intact");
    check(world.apply({detail->level,detail->cell,Quartz})>0 &&
        world.sample(detail->level,detail->cell).id==Quartz,"the excavated chip accepts a placed material");

    const Cell stone{318,61,317};
    world.apply({0,stone+Cell{0,-1,0},Stone});
    world.apply({0,stone,Quartz});
    check(world.shapedPiece({stone,0,Quartz}).has_value(),"an isolated quartz block forms a shaped crystal");
    const Vec corner=stone.v()+Vec{0.04,0.91,0.04};
    check(!world.ray(corner+Vec{0,0.17,0},{0,-1,0},0,0.20) && !world.overlaps(corner,0.03),
        "mining and collision pass through a beveled crystal corner");
    const Vec inside=stone.v()+Vec{0.51,0.50,0.51};
    check(world.ray(inside+Vec{0,0.72,0},{0,-1,0},0,0.95).has_value() &&
        world.overlaps(inside,0.04),"the visible core of the crystal remains solid");

    feet={13.4,29.002,0.66};zoom=goalZoom=0;precision=0;witness.active=false;
    world.stream(floorCell(feet),2);
    for(int i=0;i<130;i++)updateGoblin(1.0/60);
    auto& guard=goblin();
    check(guard.alert && length(guard.feet-guardChest())<2.1,
        "surface goblin runs to its chest when Delbert is near");
    auto aim=[&](Vec point){
        const auto d=point-camera();
        yaw=std::atan2(d.x,-d.z);pitch=std::atan2(d.y,std::hypot(d.x,d.z));focus();
    };
    aim(guardChest()+Vec{0,0.34,0});
    check(useGoblinChest() && !guard.looted,"nearby living goblin locks its chest");
    weapon=PulseRifle;armed=true;
    aim(guard.feet+Vec{0,0.8,0});
    fire();fire();check(guard.health==0,"rifle shots defeat the chest guard");
    armed=false;
    // The chest now has a three-guardian party. Preserve the old primary
    // rifle-hit test and explicitly mark the other two defeated before looting.
    goblin(1).health=goblin(2).health=0;
    aim(guardChest()+Vec{0,0.34,0});
    check(useGoblinChest() && guard.looted,"the chest opens after the guard is defeated");
    const auto ammo=bag[PulseRounds];
    useGoblinChest();
    check(bag[PulseRounds]==ammo,"the chest pays out once");
    auto& hold=layer(0,6);
    check(hold.at({42,1,35})==Stone && hold.at({42,2,35})==Air &&
        hold.at({42,6,35})==Lodestone && !hold.overlaps(settlement::Vault.chest,1),
        "atomic hold has clear interior beneath a solid roof");
    level=6;family=0;innerFeet={34.2,2.01,23.3};innerZoom=innerGoal=0;
    for(int i=0;i<270;i++)updateGoblin(1.0/60);
    check(goblin().alert && length(goblin().feet-settlement::Vault.chest)<2.1,
        "an atomic hold goblin can reach its guarded chest");
    flying=true;wheel(3);moveMicro(1.0/60,{});
    check(realmTier()==3 && innerSize()*subdivisions(realmTier())==1,
        "hidden realms use the same exact scale-size ratio");

    family=level=-1;flying=false;zoom=goalZoom=3;precision=3;
    for(int gate=0;gate<2;gate++){
        if(!gates[gate])gates[gate]=micro::Entrances[gate];
        feet=*gates[gate];
        check(descend(false) && family==gate && level==0 && realmTier()==0,
            "the surface portal enters the correct fresh hidden realm");
        for(int depth=0;depth<6;depth++){
            if(settlement::site(depth)){
                innerFeet=settlement::Pocket.guard+Vec{0,0.01,0};
                for(int i=0;i<210;i++)updateGoblin(1.0/60);
                check(goblin().alert && length(goblin().feet-settlement::Pocket.chest)<2.1,
                    "each hidden camp guard reaches its own chest");
            }
            innerFeet=micro::Descent;
            innerZoom=innerGoal=1;
            check(descend(false) && level==depth+1 && realmTier()==0,
                "a fresh expedition can descend through all seven layers");
        }
        check(!descend(false) && level==6,"the deepest realm stays playable at its final portal");
        family=level=-1;
    }

    zoom=goalZoom=0;precision=0;
    level=family=-1;flying=false;feet={40.5,61.001,88.5};yaw=Pi/2;pitch=0;
    world.stream(floorCell(feet),2);
    for(int x=47;x<=52;x++)for(int z=86;z<=90;z++)world.apply({0,{x,60,z},Stone});
    witness={};witness.family=family;witness.level=level;witness.active=true;
    witness.feet={49.5,64,88.5};
    updateWitness(1.0/60);
    check(witness.falling && witness.feet.y<64,"unsupported witness starts falling");
    for(int i=0;i<90;i++)updateWitness(1.0/60);
    check(!witness.falling && std::abs(witness.feet.y-61)<0.0001,
        "the witness lands on the ledge without entering solid terrain");

    feet={99.5,59.001,97.5};yaw=-Pi/2;
    world.stream(floorCell(feet),2);
    for(int x=103;x<=136;x++)for(int z=88;z<=104;z++){
        world.apply({0,{x,58,z},Stone});
        world.apply({0,{x,62,z},Stone});
    }
    witness.feet={132.5,59.001,97.5};witness.heading=-Pi/2;
    double distanceWalked=0;
    for(int i=0;i<45;i++){
        const Vec previous=witness.feet;
        updateWitness(1.0/60);
        distanceWalked+=length(witness.feet-previous);
        if(!witness.crouched || witness.climbing || witness.falling || world.overlaps(witness.feet,4.15,2.25/4.15)){
            std::cerr<<"Crouch frame "<<i<<" stance "<<witness.crouched<<" climbing "<<witness.climbing
                <<" falling "<<witness.falling<<" contact "<<witness.groundedPose<<" blocked "
                <<world.overlaps(witness.feet,4.15,2.25/4.15)<<" feet "<<witness.feet.x<<","<<witness.feet.y
                <<","<<witness.feet.z<<"\n";
            check(false,"low ceiling keeps the walking witness crouched and out of solid terrain");
        }
    }
    check(distanceWalked>2.5 && witness.groundedPose,
        "the crouching witness advances and maintains limb contacts through the low passage");

    // The roof leaves crawling clearance but prevents a climb over the wall.
    // Start against it and require a continuous route past one of its ends.
    feet={108.5,58.001,135.5};yaw=Pi/2;pitch=0;
    world.stream(floorCell(feet),2);
    for(int x=72;x<=91;x++)for(int z=126;z<=146;z++){
        world.apply({0,{x,57,z},Stone});
        world.apply({0,{x,61,z},Stone});
        if(x==79 && z>=132 && z<=139)
            for(int y=58;y<=60;y++)world.apply({0,{x,y,z},Stone});
    }
    double longestPause=0;
    for(int hz:{30,60,120}){
        witness={};witness.active=true;witness.level=level;witness.family=family;
        witness.feet={77.98,58.001,135.5};witness.heading=Pi/2;
        bool rounded=false;
        double pause=0;
        for(int frame=0;frame<hz*7 && witness.feet.x<81.5;frame++){
            const Vec previous=witness.feet;
            updateWitness(1.0/hz);
            const double speed=length(witness.feet-previous)*hz;
            pause=speed<0.12?pause+1.0/hz:0;
            longestPause=std::max(longestPause,pause);
            check(speed<14 && !world.overlaps(witness.feet,4.15,2.25/4.15),
                  "blocked crawler detours without tunnelling or teleporting");
            rounded|=witness.feet.z<131.01 || witness.feet.z>140.99;
        }
        check(rounded && witness.feet.x>=81.5,"crawler clears the end of a roofed wall and resumes approach");
    }
    check(longestPause<0.08,"detour planning does not pause the crawler");
    std::cout<<"Roofed wall at 30, 60 and 120 Hz: longest pause "<<longestPause<<" seconds.\n";

    feet={97,60.0,94.5};
    for(int x=95;x<101;x++)for(int z=93;z<=95;z++)world.apply({0,{x,58,z},Dirt});
    const Vec bed{96.95,59,94.5},pile{98.5,59,94.5};
    bag[Dirt]=0;selectMaterial(Dirt);aim(bed);button(1);
    const auto gathered=bag[Dirt];
    check(gathered==FullBlock,"primary action scoops one whole unit of dirt into inventory");
    aim(pile);button(3);
    const auto pileHit=world.ray(pile+Vec{0,1,0},{0,-1,0},0,2);
    check(bag[Dirt]==gathered-FullBlock && pileHit && pileHit->distance<0.85,
          "secondary action spends gathered dirt and raises a mound");

    const Vec seam{96.5,59,93.5};
    world.apply({0,{96,58,93},IronOre});
    bag[Pick]=bag[IronPick]=0;aim(seam);
    const auto oreStart=bag[IronOre];
    const auto revision=world.revision();
    button(1);
    check(world.revision()==revision && bag[IronOre]==oreStart,
          "surface ore still requires a pick");
    bag[Pick]=FullBlock;pickUses=2;button(1);
    const auto exposed=world.ray(seam+Vec{0,1,0},{0,-1,0},0,2);
    check((!exposed || exposed->distance>1.20) && !world.sample(0,{96,58,93}).amount &&
          bag[IronOre]==oreStart+FullBlock && pickUses==1,"mining ore removes one unit and consumes one pick use");

    const auto serial=std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path=std::filesystem::temp_directory_path()/("frontier-v060-"+std::to_string(serial)+".save");
    saveFile=path;
    check(save(),"v0.7.0 campaign saves");
    saveFile.clear();
    {
        Game loaded(renderer,data,path);
        loaded.readOnly();
        const auto savedOre=loaded.world.ray(seam+Vec{0,1,0},{0,-1,0},0,2);
        check(bool(savedOre)==bool(exposed) && (!savedOre || std::abs(savedOre->distance-exposed->distance)<1e-8) &&
              loaded.bag[IronOre]==bag[IronOre],"ore surfaces and gathered volume survive reloading");
        const auto savedPile=loaded.world.ray(pile+Vec{0,1,0},{0,-1,0},0,2);
        check(savedPile && std::abs(savedPile->distance-pileHit->distance)<1e-8 && loaded.bag[Dirt]==bag[Dirt],
              "saved dirt facets and inventory match after restarting");
        check(loaded.modernWorld && loaded.world.seed==world.seed && loaded.lootState==lootState &&
            loaded.guards[0].looted && loaded.guards[0].health==0,
            "v0.7.0 reload preserves campaign mode, seed, random state and chest state");
    }
    std::filesystem::remove(path);
    std::cout<<"Verified v0.7.0 starter gear, discrete scale, edited slopes, hidden routes, goblin chests, witness gravity and crouch, and campaign saves.\n";
}

void Game::benchmark(){
    using Clock=std::chrono::steady_clock;
    const auto measure=[&](const char* name,bool traversal=false){
        const double heading=yaw;
        constexpr int frames=90;
        for(int i=0;i<30;i++)draw(1280,720);
        renderer.settle();
        std::vector<double> samples;
        samples.reserve(frames);
        for(int i=0;i<frames;i++){
            const auto start=Clock::now();
            yaw=heading+std::sin(i*0.09)*0.6;
            if(traversal){
                if(i%30==0)wheel(i%60==0?12:-12);
                update(1.0/60,{1,0,0,true,false,false});
            }
            draw(1280,720);
            renderer.settle();
            samples.push_back(std::chrono::duration<double>(Clock::now()-start).count());
        }
        const double seconds=std::accumulate(samples.begin(),samples.end(),0.0);
        std::sort(samples.begin(),samples.end());
        std::cout<<name<<"   "<<number(frames/seconds,1)<<" FPS   "<<number(samples[85]*1000,1)
            <<" ms p95 at 1280 x 720, "<<renderer.renderScale()<<"% 3D\n";
    };
    feet={0.5,29,14.7};yaw=Pi;pitch=-0.08;
    world.stream(floorCell(feet),2);measure("Surface");
    feet={74.8,17.01,23.6};yaw=Pi/2;pitch=0.05;precision=3;zoom=goalZoom=3;
    world.stream(floorCell(feet),2);measure("Shrunk quarry");
    family=0;level=expedition::Village;innerFeet={24.5,5.02,16.5};yaw=Pi-0.3;pitch=0;
    measure("Elderbough");
    family=level=-1;flying=true;
    feet={54.8,17.08,23.5};yaw=Pi/2;pitch=-0.12;
    zoom=goalZoom=0;precision=0;
    world.stream(floorCell(feet),2);recoverPosition();measure("Shrink traversal",true);
}

void modelTests(const std::filesystem::path& data){
    auto require=[](bool ok,const char* label){if(!ok)throw std::runtime_error(std::string("Test failed: ")+label);};
    {
        World blocks([](Cell cell){return cell.y==8?int(Timber):int(Air);});
        contact::Rig rig;
        contact::Frame frame{{.5,9.000001,.5},1,0};
        contact::Queries scene;
        scene.cast=[&](Vec p,Vec dir,double reach)->std::optional<contact::Surface>{
            const auto hit=blocks.ray(p,dir,0,reach);
            if(!hit)return {};
            return contact::Surface{p+dir*hit->distance,hit->normal.v()};
        };
        const auto pose=[&]{rig.update(frame,false,false,false,{},5,1.0/60,scene);};
        pose();
        const Vec plant=rig.limbs[0].support.point;
        require(rig.pose.legs[0].planted && rig.pose.legs[1].planted,"IK plants both feet on world geometry");
        frame.origin.x+=0.10;frame.heading=0.10;pose();
        const auto& leg=rig.pose.legs[0];
        require(leg.planted && length(frame.point(leg.tip)-(plant+Vec{0,leg.pad,0}))<1e-7 &&
            std::abs(length(leg.joint-leg.root)-rig.anatomy.leg.upper)<1e-8 &&
            std::abs(length(leg.tip-leg.joint)-rig.anatomy.leg.lower)<1e-8,
            "a planted foot holds position and bone lengths while its root translates and turns");
        blocks.apply({0,floorCell(plant-Vec{0,0.01,0}),Air});pose();
        require(!rig.pose.legs[0].planted,"an excavated support releases the foot");
        blocks.apply({0,{0,12,1},Timber});
        frame.heading=0;frame.origin.x=.75;rig.ready=false;pose();
        require(rig.pose.arms[0].planted && rig.limbs[2].support.normal.z<-.9,
            "an outstretched palm touches a real wall face");
    }
    {
        World blocks([](Cell){return int(Air);});
        const Cell root{31,5,31},child=root*4;
        blocks.apply({1,child,Timber});blocks.apply({1,child+Cell{1,0,0},Timber});
        Geometry mesh;
        terrain::editedRoot(mesh,blocks,root,root.v(),{1,1,1},false);
        // Six outer surfaces remain after greedy merging; each now has four
        // inward-facing geological facets rather than two coplanar triangles.
        require(mesh.points.size() >= 72,
            "two adjacent edited cells share six watertight chipped exterior surfaces");
        require(blocks.meshVersion({1,0,1})==blocks.revision(),"corner edits invalidate the diagonal proxy");
    }
    World world(data);
    require(world.validAssets(),"all 128 real row files");
    World first(data),repeat(data),different(data);
    first.setSeed(8420);repeat.setSeed(8420);different.setSeed(8421);
    first.stream({384,33,384},0);repeat.stream({384,33,384},0);different.stream({384,33,384},0);
    bool varied=false;
    for(int z=386;z<416;z+=4)for(int x=386;x<416;x+=4)for(int y=16;y<58;y++){
        const auto material=first.sample(0,{x,y,z}).id;
        require(material==repeat.sample(0,{x,y,z}).id,"seed replay is deterministic");
        varied|=material!=different.sample(0,{x,y,z}).id;
    }
    require(varied,"different seeds reshape the frontier");
    world.stream({0,28,0},2);
    require(world.sample(0,{0,28,-7}).id==CampTile,"starting floor");
    const Cell coarse{-3,30,-4};
    require(world.sample(0,coarse).id==Timber,"supply bay");
    const Cell chip=coarse*4+Cell{1,3,2};
    const Cell grit=chip*4+Cell{2,3,2};
    require(world.apply({2,grit,Air})==-volume(2),"remove one grain");
    require(world.sample(0,coarse).amount==FullVolume-volume(2),"parent carries removal");
    require(world.sample(2,grit).amount==0,"grain stays deleted");
    require(world.sample(2,grit+Cell{1,0,0}).id==Timber,"adjacent grain stays present");
    require(world.apply({2,grit,Timber})==volume(2),"restoration costs exact amount");
    require(world.sample(0,coarse).amount==FullVolume,"no duplicated material");
    require(world.apply({1,chip,Air})==-volume(1),"one child removed");
    auto hit=world.ray({-2.625,32,-3.375},{0,-1,0},1,5);
    require(hit && hit->cell==chip-Cell{0,1,0},"ray enters mined hole");
    constexpr int deep=FineDepth;
    constexpr int n=subdivisions(deep);
    constexpr double tiny=1.0/n;
    const Cell empty{5,59,-8};
    require(world.sample(0,empty).id==Air,"fixture air cell");
    const Cell seed=empty*n+Cell{96,24,154};
    require(world.apply({deep,seed,Stone})==volume(deep),"place one tiny stone");
    require(world.sample(0,empty).amount==1 && world.editedChildren(empty),"parent records one deepest child");
    Geometry smallMesh;
    terrain::editedRoot(smallMesh,world,empty,empty.v(),{1,1,1},!world.sculptedTerrain);
    using Corner=std::array<float,3>;
    std::map<std::pair<Corner,Corner>,int> edges;
    for(std::size_t i=0;i<smallMesh.points.size();i+=3)for(int j=0;j<3;j++){
        const auto& a=smallMesh.points[i+j];const auto& b=smallMesh.points[i+(j+1)%3];
        Corner first{a.x,a.y,a.z},next{b.x,b.y,b.z};
        ++edges[std::minmax(first,next)];
    }
    require(!edges.empty() && std::all_of(edges.begin(),edges.end(),[](const auto& e){return e.second==2;}),
            "single child has a closed two-manifold surface");
    Geometry tinyFacet;
    const double sampleSize=1.0/subdivisions(FineDepth);
    tinyFacet.triangle({},{sampleSize*0.02,0,0},{0,sampleSize*0.02,0},Stone,{1,1,1});
    require(tinyFacet.points.size()==3 && tinyFacet.points[0].nz>0.99f,
            "tiny facets retain stable normals at maximum shrink");
    const auto meshBox=geometryBounds(smallMesh);
    const auto expected=Leaf{seed,deep,Stone}.bounds();
    for(int axis=0;axis<3;axis++)require(meshBox.lo[axis]>=expected.lo[axis]-empty[axis]-1e-9 &&
        meshBox.hi[axis]<=expected.hi[axis]-empty[axis]+1e-9 && meshBox.hi[axis]-meshBox.lo[axis]>.5*tiny,
        "faceted stone fits its leaf at the smallest scale");
    const auto center=(expected.lo+expected.hi)*0.5;
    require(!world.overlaps(empty.v()+Vec{0.87,0.18,0.12},0.5),"empty side of a partially occupied parent is walkable");
    require(!world.ray(empty.v()+Vec{0.87,1.2,0.12},{0,-1,0},0,1.3),"coarse ray misses empty part of a sparse parent");
    for(int res=0;res<=deep;res++){
        const auto exact=world.ray(center+Vec{0,0.7,0},{0,-1,0},res,1.3);
        require(exact && exact->level==deep && exact->cell==seed,"all pick resolutions target the same tiny placement");
    }
    const Vec left={expected.lo.x-0.42,center.y-0.001,center.z};
    const double body=0.0016;
    const double move=world.sweep(left,body,0,2.5);
    require(move>0.41 && move<0.42,"swept motion hits the real leaf, not the parent");
    require(!world.overlaps(left+Vec{move,0,0},body),"sweep ends outside thin voxel");
    require(world.sweep(left+Vec{move,0,0},body,0,0.30)<1e-6,"a second frame cannot tunnel through the leaf");
    require(world.sweep(left+Vec{0,0,0.10},body,0,2.5)>2.4,"a sweep beside the leaf passes through parent air");

    const Cell carved{8,59,-8};
    world.apply({0,carved,Stone});
    const Cell hole=carved*n+Cell{70,n-1,122};
    require(world.apply({deep,hole,Air})==-1,"remove tiny cube from top of large cube");
    const auto voidBox=Leaf{hole,deep,Air}.bounds();
    const auto holeCenter=(voidBox.lo+voidBox.hi)*0.5;
    auto throughHole=world.ray(holeCenter+Vec{0,0.5,0},{0,-1,0},0,1);
    require(throughHole && throughHole->distance>0.5 && throughHole->level==deep &&
            throughHole->cell==hole+Cell{0,-1,0},"grown cursor reaches the physical bottom of the pore");
    Geometry holeMesh;
    terrain::editedRoot(holeMesh,world,carved,carved.v(),{1,1,1},!world.sculptedTerrain);
    const Vec probe=holeCenter-carved.v();
    bool cap=false,bottom=false;
    for(std::size_t i=0;i<holeMesh.points.size();i+=3){
        const auto& a=holeMesh.points[i];const auto& b=holeMesh.points[i+1];const auto& c=holeMesh.points[i+2];
        const bool covers=std::min({a.x,b.x,c.x})<probe.x && std::max({a.x,b.x,c.x})>probe.x &&
            std::min({a.z,b.z,c.z})<probe.z && std::max({a.z,b.z,c.z})>probe.z;
        if(covers && a.y==b.y && b.y==c.y){
            cap|=a.y==1;
            bottom|=a.y==1-tiny;
        }
    }
    require(!cap && bottom,"chunk mesh leaves the opening uncovered at normal scale");

    const Cell boundary{31,59,-8};
    world.apply({deep,boundary*n+Cell{n-1,0,64},Dirt});
    require(world.meshVersion({1,0,-1})==world.revision(),"fine edit invalidates neighboring chunk at seam");
    const Bounds nearby{{0,120,0},{8,128,8}};
    const auto beforeRemoteEdit=world.detailVersion(nearby,1);
    require(world.apply({0,{65,58,65},Quartz})>0,"remote detail edit fixture");
    require(world.detailVersion(nearby,1)==beforeRemoteEdit,"remote edit leaves nearby detail cache valid");
    require(world.apply({0,{12,58,12},Quartz})!=0,"same-tile remote edit fixture");
    require(world.detailVersion(nearby,1)==beforeRemoteEdit,"same-tile remote edit leaves fine geometry cached");
    require(world.apply({0,{-1,32,0},Quartz})!=0,"neighbor detail edit fixture");
    require(world.detailVersion(nearby,1)==world.revision(),"detail mesh includes its padded neighbor");
    world.stream({-500,29,-500},2);
    world.stream({0,29,0},2);
    require(world.sample(deep,seed).id==Stone && world.sample(deep,hole).id==Air,
            "both sparse edits survive chunk eviction and return");
    world.stream({65,17,23},2);
    require(world.overlaps({67.5,17.01,23.5},1.0),"tunnel blocks normal body");
    require(!world.overlaps({67.5,17.01,23.5},0.16),"shrunk body fits tunnel");
    std::unordered_map<Cell,Device,CellHash> circuit{{{0,29,12},{Switch,true,{0,1,0}}},{{0,29,13},{Lamp,true,{0,1,0}}}};
    auto net=solveNetwork(circuit,{0,29,11});
    require(std::abs(net.conductance-1/8.64)<1e-10,"circuit resistance");
    circuit[{0,29,12}].closed=false;
    require(solveNetwork(circuit,{0,29,11}).conductance==0,"switch cuts power");
    GeneratorState loaded,open;
    double loadSpeed=0,openSpeed=0,delivered=0;
    for(int i=0;i<900;i++){
        loaded.advance(1.0/180,net.conductance);open.advance(1.0/180,0);
        if(i>=300){loadSpeed+=loaded.speed;openSpeed+=open.speed;delivered+=loaded.gridWatts;}
    }
    require(loadSpeed<openSpeed && delivered>0,"power and braking");
    std::cout<<"Verified exact chunk geometry, sparse holes and placements, swept collision, chunk seams, ray picking and generator.\n";
}

void Game::verifySession(const std::filesystem::path& data){
    using namespace platform;
    combatEnabled=false;
    auto check=[](bool passed,const std::string& part){if(!passed)throw std::runtime_error("Play test: "+part);};
    auto aim=[&](Vec where){
        const auto dir=where-camera();
        const double heading=std::atan2(dir.x,-dir.z),slope=std::atan2(dir.y,std::hypot(dir.x,dir.z));
        look(std::remainder(heading-yaw,2*Pi)/0.0025,(pitch-slope)/0.0025);
        focus();
    };
    auto walk=[&](double x,double z,int count=1100){
        for(int i=0;i<count && std::hypot(feet.x-x,feet.z-z)>0.10;i++){
            aim({x,camera().y,z});
            update(1.0/45,{1,0,0,false,false});
        }
    };
    auto go=[&](double z){
        walk(0.5,z);
        check(std::abs(feet.z-z)<0.25,"reach trail at "+std::to_string(z)+" (at "+std::to_string(feet.z)+")");
    };
    auto mine=[&](int x,int y,int z){
        aim(Vec{double(x)+0.5,double(y)+0.5,double(z)+0.5});
        key(I);
    };
    auto bay=[&](int x,int y,int z){
        for(int dx=0;dx<3;dx++)for(int dz:{0,-1,1})mine(x+(x<0?-dx:dx),y,z+dz);
    };
    auto make=[&](int index,int repeats=1){
        if(machineRecipe(recipes[index].makes)){
            aim(CampStation.v()+Vec{0.5,0.54,0.5});key(E);
        }else key(C);
        check(crafting && visibleRecipe(index),"open the appropriate recipe station");
        for(int tries=0;recipe!=index && tries<int(recipes.size());tries++)key(recipe>index?Up:Down);
        check(recipe==index,"navigate to the expected recipe");
        for(int i=0;i<repeats;i++)key(Enter);
        const int product=recipes[index].makes;
        check(product==Generator?built:bag[product]>0,std::string("craft ")+recipes[index].label+" ("+status+")");
        if(crafting)key(C);
    };
    go(-4);
    bay(-3,30,-4);
    go(1);
    bay(-3,30,1);
    check(bag[Timber]>=FullBlock*6ll && bag[Stone]>=FullBlock*7ll,"gather starter timber and stone");
    make(0);
    go(-4);bay(3,30,-4);
    go(1);bay(3,30,1);
    go(5);bay(-3,30,5);bay(3,30,5);
    make(1);
    go(14);
    for(int x:{-4,-5,-6})for(int z=13;z<=16;z++)mine(x,29,z);
    make(2,2);make(3,2);make(4);make(5);make(6);make(7);
    check(built && atGenerator(),"assemble the generator at camp");
    make(2,2);make(8);
    go(15);
    aim({0.5,28.99,12.5});button(3);
    check(devices.contains({0,29,12}),"mount cable beside generator");
    make(9);
    aim({0.5,28.99,13.5});button(3);
    check(devices.contains({0,29,13}),"mount lamp after cable");
    for(int i=0;i<180;i++)update(1.0/90,{});
    check(lightAt({0,29,13})>0.25 && wired,"field lamp receives power");
    go(24);
    walk(56,23.5);
    check(std::abs(feet.x-56)<0.45 && feet.y<21,"follow the trail down to the quarry");
    walk(75,23.5,440);
    check(feet.x<67,"tunnel blocks full-sized player");
    wheel(4);
    for(int i=0;i<45;i++)update(1.0/45,{});
    walk(76,23.5,3400);
    check(feet.x>74 && crawled,"shrink to enter the quartz chamber");
    wheel(3);
    for(int i=0;i<70;i++)update(1.0/45,{});
    aim(feet-Vec{0,0.5,0});
    check(target.has_value() && target->level>=2,"pick a fine block on chamber floor");
    const auto piece=*target;
    const auto before=bag[harvestMaterial(piece.id)];
    key(I);
    check(world.sample(piece.level,piece.cell).amount==0
          && bag[harvestMaterial(piece.id)]==before+FullBlock,"harvest a full resource from a fine block");
    check(world.sample(0,parent(piece.cell,subdivisions(piece.level))).amount==FullVolume-volume(piece.level),
          "keep larger block and preserve mined void");
    const auto serial=std::chrono::steady_clock::now().time_since_epoch().count();
    saveFile=std::filesystem::temp_directory_path()/("frontier-qa-"+std::to_string(serial)+".save");
    save();
    const auto temporary=std::exchange(saveFile,{});
    check(std::filesystem::exists(temporary),"write native save");
    world.clearEdits();bag.fill(0);devices.clear();built=false;
    saveFile=temporary;load();saveFile.clear();
    std::filesystem::remove(temporary);
    check(world.sample(piece.level,piece.cell).amount==0
          && bag[harvestMaterial(piece.id)]==before+FullBlock,"reload fine edit and full harvest reward");
    check(built && devices.contains({0,29,13}),"reload generator and field wiring");
    const double oldY=feet.y;
    key(V);
    for(int i=0;i<14;i++)update(1.0/45,{0,0,1,false,false,true});
    check(flying && feet.y>oldY+0.03,"flight raises the player at reduced scale");
    const double peak=feet.y;
    for(int i=0;i<14;i++)update(1.0/45,{0,0,-1,false,false,true});
    check(feet.y<peak-0.025,"flight descends with Q motion");
    key(V);
    check(!flying,"flight toggles off");
    key(E);
    check(level<0 && !ownsWeapon(),"portal rejects travelers without a pistol or rifle");
    bag[PulseRifle]=FullBlock;
    key(E);
    check(level==0 && family==1,"enter the quarry portal while shrunk");
    auto innerWalk=[&](double x,double z){
        for(int frame=0;frame<900 && std::hypot(innerFeet.x-x,innerFeet.z-z)>0.12;frame++){
            aim({x,camera().y,z});
            update(1.0/45,{1,0,0,false,false});
        }
        check(std::hypot(innerFeet.x-x,innerFeet.z-z)<0.20,"navigate hidden world "+std::to_string(level+1));
    };
    for(int depth=0;depth<micro::Count-1;depth++){
        innerWalk(micro::Descent.x,micro::Descent.z-0.4);
        wheel(4);
        for(int i=0;i<40 && level==depth;i++)update(1.0/45,{});
        check(level==depth+1,"shrink through gate to hidden world "+std::to_string(depth+2));
    }
    bag[PulseRifle]=0;
    innerWalk(27.5,5);
    auto& atomic=layer(family,level);
    bool mined=false;
    Cell stolen{};
    for(int x=29;x<32 && !mined;x++)for(int y=5;y<10 && !mined;y++)
        for(int z=4;z<9 && !mined;z++){
            const Cell cell{x,y,z};
            aim(cell.v()+Vec{0.5,0.5,0.5});
            if(!target || target->cell!=cell || protectedMaterial(target->id))continue;
            const int id=harvestMaterial(target->id);
            const auto count=bag[id];
            key(I);
            if(atomic.at(cell)==Air && bag[id]==count+FullBlock){mined=true;stolen=cell;}
        }
    check(mined && level==6,"harvest an atom site for a full resource");

    saveFile=temporary;save();
    for(auto& set:nests)for(auto& room:set)room.reset();
    level=family=-1;
    load();saveFile.clear();
    std::filesystem::remove(temporary);
    check(level==6 && family==1 && layer(family,level).at(stolen)==Air,"reload atomic world and depleted site");
    for(int depth=micro::Count-1;depth>=0;depth--){
        key(B);check(level==depth-1,"ascend each nested layer");
    }
    check(entrance()==1,"return to the same quarry entrance");
    feet={24.50,61.0002,-10.50};
    world.stream(floorCell(feet),2);
    for(int x=23;x<=25;x++)for(int z=-12;z<=-10;z++)world.apply({0,{x,60,z},Stone});
    zoom=goalZoom=3.15;precision=3;vertical.reset();flying=false;
    aim({feet.x+0.067,60.99,feet.z});
    check(target && target->level==3 && target->id==Stone,"aim at a small part of a full cube");
    const Hit chipCut=*target;
    const auto balance=bag[Stone];
    key(I);
    check(bag[Stone]==balance+FullBlock && !world.sample(chipCut.level,chipCut.cell).amount,
        "exact mining of small stone gives a whole inventory block");
    aim({feet.x+0.094,60.99,feet.z+0.05});
    check(target && target->normal==Cell{0,1,0},"aim to place on empty floor-facing cell");
    const Cell placed=target->previous;
    const Cell outer=parent(placed,subdivisions(3));
    check(!world.sample(0,outer).amount,"small placement starts in an otherwise empty cube");
    key(Two);
    button(3,true);
    check(bag[Stone]==balance && world.sample(3,placed).id==Stone &&
          world.sample(0,outer).amount==volume(3),"exact placement adds only a small cube and consumes one inventory block");
    const Vec placeOrigin=outer.v();
    Geometry beforeGrow;
    terrain::editedRoot(beforeGrow,world,outer,placeOrigin,{1,1,1},!world.sculptedTerrain);
    feet.x-=0.5;
    wheel(-99);
    for(int frame=0;frame<45;frame++)update(0.05,{});
    check(zoom<0.001,"grow completely after moving away from placement");
    Geometry afterGrow;
    terrain::editedRoot(afterGrow,world,outer,placeOrigin,{1,1,1},!world.sculptedTerrain);
    check(!beforeGrow.points.empty() && beforeGrow.points.size()==afterGrow.points.size() &&
        length(geometryBounds(beforeGrow).hi-geometryBounds(afterGrow).hi)<1e-12 &&
        world.sample(3,chipCut.cell).amount==0,"mesh dimensions and removed cell stay unchanged after growth");
    const Cell oreRoot{25,60,-11};
    world.apply({0,oreRoot,CopperOre});
    feet={25.42,61.0002,-10.50};
    zoom=goalZoom=3.15;precision=3;
    aim({feet.x+0.064,60.99,feet.z});
    check(target && target->id==CopperOre && target->level==3,"aim at tiny ore");
    const Cell oreHole=target->cell;
    const auto oreCount=bag[CopperOre];
    key(I);
    check(bag[CopperOre]==oreCount+FullBlock,"a tiny ore drop is one whole block");
    feet={24.30,61.0002,-10.48};
    wheel(-100);
    for(int frame=0;frame<90;frame++)update(1.0/45,{});
    check(zoom<0.001,"return to full size with mined ore in backpack");
    // Selected Stone is at picker index 1; move to Copper ore at index 4.
    key(H);key(Down);key(Down);key(Left);key(Enter);
    check(placement[selected]==CopperOre,"build palette selects harvested ore");
    aim({23.45,60.90,-11.12});
    check(target && target->level==0 && target->normal==Cell{0,1,0},"aim to place a full ore block");
    const auto oe=target->previous;
    button(3,true);
    check(bag[CopperOre]==oreCount && world.sample(0,oe).id==CopperOre &&
        world.sample(0,oe).amount==FullVolume && world.sample(3,oreHole).amount==0,
        "ore collected at tiny scale can place a full block without filling the mined hole");
    const Cell mixed{24,61,-12};
    const Cell shard=mixed*4+Cell{1,0,3};
    world.apply({1,shard,Stone});
    world.apply({1,mixed*4+Cell{2,0,3},Dirt});
    const auto stoneBefore=bag[Stone],dirtBefore=bag[Dirt];
    aim(Leaf{shard,1,Stone}.bounds().lo+Vec{0.125,0.125,0.125});
    check(target && target->level==1 && target->cell==shard,"coarse aim hits a real shard");
    key(I);
    check(world.sample(1,shard).id==Stone && bag[Stone]==stoneBefore &&
          bag[Dirt]==dirtBefore,"mixed fragments cannot be exchanged for a full-size resource");
    const double floorHeight=feet.y;
    for(int i=0;i<60;i++){
        wheel(i%2?100:-100);
        for(int step=0;step<7;step++){
            update(0.12,{});
            check(!world.overlaps(feet,body()),"rapid scroll keeps player hull clear");
        }
    }
    check(std::abs(feet.y-floorHeight)<0.002,"rapidly shrinking does not sink below the floor");

    // Legacy builds could save the sole a few centimeters into the support.
    feet.y=60.982;
    update(1.0/30,{});
    check(!world.overlaps(feet,body()) && feet.y>=61,"recover a legacy position partly inside the floor");
    feet={67.5,17.0002,23.5};
    world.stream(floorCell(feet),2);
    precision=3;zoom=goalZoom=3.15;vertical.reset();
    wheel(-100);
    for(int i=0;i<100;i++){
        update(0.10,{});
        check(!world.overlaps(feet,body()),"growth stops short of tunnel roof");
    }
    check(zoom>0 && zoom<1.0 && feet.y>16.999 && world.overlaps(feet,body()*1.02),
        "tunnel permits safe growth only as far as it fits (zoom "+number(zoom,4)+")");
    const auto eye=camera();
    const Vec clearance{0.02*body(),0.02*body(),0.02*body()};
    check(world.visitMatter({eye-clearance,eye+clearance},[](Leaf leaf){return leaf.material==Water;}),
        "collider protects the view point from the ceiling too");
    saveFile=temporary;save();
    world.clearEdits();bag.fill(0);load();saveFile.clear();
    std::filesystem::remove(temporary);
    check(world.sample(3,placed).id==Stone && world.sample(0,outer).amount==volume(3) &&
        world.sample(3,chipCut.cell).amount==0 && world.sample(3,oreHole).amount==0 &&
        world.sample(0,oe).id==CopperOre,"reload at another scale preserves the little block, both holes and the full ore block");
    {
        const std::string interrupted="FRONTIER 3\nstate 1 0 0 0 6 0 1\n";
        std::ofstream legacy(temporary);
        legacy<<interrupted;
        legacy.close();
        saveFile=temporary;load();save();
        std::ifstream kept(temporary);
        const std::string original((std::istreambuf_iterator<char>(kept)),{});
        check(original==interrupted && saveFile.empty(),"interrupted legacy save remains untouched");
        kept.close();std::filesystem::remove(temporary);
    }
    // Exercise the new workshop recipes and complete an electrical mining cycle.
    feet={0.5,29,13.5};zoom=goalZoom=0;precision=0;
    world.stream(floorCell(feet),2);
    bag[Iron]=132LL*FullBlock;
    bag[Timber]=20LL*FullBlock;
    bag[Quartz]=2LL*FullBlock;
    workshop=Workshop::Workbench;
    recipe=11;craft();
    check(bag[IronPick]==FullBlock,"iron pick recipe");
    workshop=Workshop::Machine;
    recipe=12;craft();
    check(bag[Fabricator]==FullBlock,"machine crafter recipe");
    const Cell crafter{2,29,13};
    devices.emplace(crafter,Device{Fabricator});
    recipe=13;craft();
    check(bag[Quarry]==FullBlock,"quarry recipe near crafter");
    recipe=14;craft();
    check(bag[North]==FullBlock,"craft North at the Machine Crafter");
    recipe=28;
    const auto ironBeforeWell=bag[Iron];craft();
    check(bag[ResourceWell]==FullBlock && ironBeforeWell-bag[Iron]==5*FullBlock,"resource generator costs five iron");
    check(!visibleRecipe(21) && visibleRecipe(15),"machine crafter offers machines and keeps furniture out");
    const Cell mineCell{1,29,11},deposit{2,27,11};
    devices[mineCell]=Device{Quarry,true,{0,1,0},IronOre,0};
    world.apply({0,deposit,IronOre});
    refreshGrid();
    check(network.quarries.contains(mineCell),"quarry adds a connected electrical load");
    const auto ironOreBefore=bag[IronOre];
    aim(mineCell.v()+Vec{0.5,0.25,0.5});
    key(E);
    check(machineConsole==mineCell && !wantsCapture(),"E opens Quarry controls and releases the mouse");
    const Cell northCell{1,29,14},wellCell{0,29,16};
    devices[northCell]=Device{North};
    bag[North]=0;
    devices[wellCell]=Device{ResourceWell,false};
    for(int i=0;i<120 && devices.at(mineCell).energy<=0;i++)update(1.0/60,{});
    const auto quarryProgress=devices.at(mineCell).energy;
    const auto cycleBudget=devices.at(mineCell).cycleEnergy;
    check(quarryProgress>0 && quarryProgress<cycleBudget,"Quarry charges toward a fixed cycle budget");
    key(Enter);
    check(!network.quarries.contains(mineCell),"paused Quarry removes its electrical load");
    for(int i=0;i<180;i++)update(1.0/60,{});
    check(devices.at(mineCell).energy==quarryProgress && bag[IronOre]==ironOreBefore,
        "pausing preserves progress and prevents mining");
    key(Escape);
    check(wantsCapture(),"closing Quarry controls restores look");
    const auto beforeNorth=bag[Iron];
    recipe=14;craft();
    check(!bag[North] && bag[Iron]==beforeNorth,"cannot craft a second North");
    aim(northCell.v()+Vec{0.5,1.36,0.5});
    button(3);
    check(!machineConsole && devices.contains(northCell),"North interaction opens no dialogue or machine control");
    feet={0.5,29.001,15.15};
    aim(wellCell.v()+Vec{0.5,0.44,0.5});
    key(E);
    check(machineConsole==wellCell,"focus opens resource generator controls (aimed "+
        (deviceTarget?std::to_string(deviceTarget->x)+","+std::to_string(deviceTarget->y)+","+std::to_string(deviceTarget->z):"terrain")+")");
    key(Left);key(Left);
    check(devices.at(wellCell).material==Quartz,"choose generated resource");
    key(Enter);
    const auto quartzBefore=bag[Quartz];
    workWells(0.25);workWells(0.50);
    check(bag[Quartz]==quartzBefore,"resource generator waits until a full second");
    workWells(0.25);workWells(0.40);
    check(bag[Quartz]==quartzBefore+FullBlock,"resource generator produces exactly one resource per second");
    key(Escape);
    saveFile=temporary;
    save();
    {
        Game restored(renderer,data,temporary);
        restored.readOnly();
        const auto& quarry=restored.devices.at(mineCell);
        const auto& well=restored.devices.at(wellCell);
        check(!quarry.closed && std::abs(quarry.energy-quarryProgress)<1e-7 && quarry.cycleEnergy==cycleBudget,
            "Quarry pause, energy and fixed budget survive reload");
        check(restored.devices.contains(northCell) && well.closed && well.material==Quartz && std::abs(well.energy-0.4)<1e-7,
            "North location and resource production settings survive reload");
    }
    saveFile.clear();
    {
        std::ofstream legacy(temporary);
        // v0.5.4 initially saved the target and joules without a budget or job.
        legacy<<"FRONTIER 3\nstate 1 0 0 0 6 0 1\n"
              <<"dev 1 29 11 "<<int(Quarry)<<" 1 0 1 0 "<<int(IronOre)<<" 2\n"
              <<"dev 1 29 14 "<<int(North)<<" 1 0 1 0 "<<int(Stone)<<" 0\nend\n";
    }
    {
        Game legacy(renderer,data,temporary);
        legacy.readOnly();
        check(legacy.devices.at(mineCell).energy==2 && legacy.devices.at(mineCell).material==IronOre,
            "legacy Quarry energy and material load");
        check(legacy.devices.at(northCell).id==North,
            "legacy North material remains compatible");
    }
    std::filesystem::remove(temporary);
    aim(mineCell.v()+Vec{0.5,0.25,0.5});
    key(E);key(Enter);key(Escape);
    check(network.quarries.contains(mineCell),"resumed Quarry reconnects its electrical load");
    const auto field=generator.field;
    generator.field=0;
    update(1.0/60,{});
    check(devices.at(mineCell).energy==quarryProgress,"power cutoff never spends smoothed voltage as real energy");
    generator.field=field;
    for(int i=0;i<1200 && bag[IronOre]==ironOreBefore;i++)update(1.0/60,{});
    check(bag[IronOre]==ironOreBefore+FullBlock && !world.sample(0,deposit).amount,
        "powered Quarry removes a matching physical deposit");
    bag[Iron]=40LL*FullBlock;bag[Copper]=30LL*FullBlock;bag[Quartz]=18LL*FullBlock;
    bag[Coal]=8LL*FullBlock;bag[Timber]=20LL*FullBlock;
    workshop=Workshop::Machine;recipe=15;craft();
    check(bag[PrismPress]==FullBlock,"craft Prism Press through camp machine crafter");
    workshop=Workshop::Press;recipe=17;craft();
    check(!bag[PulseCrystal],"pulse crystals require a mounted press");
    const Cell press{2,29,15};
    devices[press]=Device{PrismPress};
    recipe=16;craft();
    recipe=17;craft();
    recipe=18;craft();
    recipe=19;craft();
    check(!visibleRecipe(19) && !bag[QuartzSight],"obsolete separate sight cannot be crafted");
    recipe=27;craft();
    workshop=Workshop::Machine;recipe=20;craft();
    check(bag[PulsePistol]==FullBlock && bag[PulseRifle]==FullBlock && bag[PulseRounds]==5LL*FullBlock &&
        !bag[QuartzSight] && bag[PowerPack]==FullBlock,"craft pistol, rifle with permanent optic, power pack and five rounds");
    recipe=29;craft();
    check(bag[HouseBuilder]==FullBlock,"craft a House Builder");
    workshop=Workshop::House;
    const Cell builder{0,29,10};
    devices[builder]=Device{HouseBuilder};
    workshopCell=builder;
    recipe=24;craft();
    check(!bag[Plank],"House Builder needs connected power");
    refreshGrid();generator.rms2=5;
    recipe=24;craft();recipe=25;craft();
    check(housePower(builder)>0.025 && bag[Plank]==4LL*FullBlock && bag[RoofTile]==4LL*FullBlock,
        "powered House Builder crafts furnishings exclusively");
    bag[IronPick]=0;bag[Pick]=FullBlock;pickUses=5;
    workshop=Workshop::Workbench;recipe=26;craft();
    check(!bag[Pick] && bag[IronPick]==FullBlock,"temper a worn stone pick into an infinite iron pick");
    key(X);key(Z);
    check(armed && weapon==PulsePistol && !sightOn,"pistol cannot attach a sight");
    key(X);key(Z);
    check(armed && weapon==PulseRifle && sightOn,"rifle has its permanent optic with an empty battery");
    generator.rms2=10;
    for(int i=0;i<18;i++)updateExpedition(1.0/60);
    check(battery>0,"camp generator recharges wearable battery");
    const double charged=battery;
    aiming=true;
    family=0;level=0;innerFeet=micro::Arrival;innerZoom=innerGoal=0;
    updateExpedition(1.0/60);
    check(battery<charged,"powered quartz sight draws charge outside camp");
    for(int set=0;set<2;set++)for(int depth=0;depth<micro::Count;depth++){
        auto& critters=wildlife(set,depth);
        std::set<Cell> spawns;
        for(const auto& creature:critters.enemies)spawns.insert(floorCell(creature.feet));
        check(critters.defeated.none() && spawns.size()==expedition::Hostiles,"20 unique crawler spawns per layer");
    }
    auto& village=layer(0,expedition::Village);
    const Cell villageDoor{13,5,11};
    const Vec doorway{13.50,5.001,11.52};
    check(village.at(villageDoor)==Door && village.overlaps(doorway,1),"village includes a functional closed door");
    check(village.set(villageDoor,OpenDoor) && !village.overlaps(doorway,1),"open door leaves a clear passage");
    const Vec childPosition=micro::Arrival+Vec{4,2,-1};
    const Vec mapped=micro::toParent(0,expedition::Village,childPosition);
    check(length(mapped-micro::Descent-(childPosition-micro::Arrival)*micro::parentScale(expedition::Village))<1e-12,
        "layer coordinates retain measured parent-space offsets");

    combatEnabled=true;
    auto& critters=wildlife(family,level);
    critters.defeated.set();critters.defeated.reset(0);
    auto& crawler=critters.enemies[0];
    crawler.feet={24.5,5.001,8.5};crawler.health=60;
    const double farSeparation=length(crawler.feet-innerFeet);
    updateExpedition(0.12);
    check(length(crawler.feet-innerFeet)<farSeparation,"crawler navigates toward Delbert");
    crawler.feet={24.5,5.001,5.51};crawler.attack=0;
    updateExpedition(0.015);
    check(health==80,"crawler inflicts exactly 20 melee damage");
    crawler.feet={24.5,5.001,8.5};
    const Cell shield{24,6,6};
    auto& combatRoom=layer(family,level);
    check(combatRoom.set(shield,Stone),"place a test shield in crawler arena");
    aim(crawler.feet+Vec{0,0.79,0});
    button(1);
    check(!critters.defeated[0] && bag[PulseRounds]==4LL*FullBlock,"terrain stops a fired round");
    combatRoom.set(shield,Air);
    button(1);
    check(critters.defeated[0] && bag[PulseRounds]==3LL*FullBlock,"one click fires one round and defeats the targeted crawler");
    saveFile=temporary;save();saveFile.clear();
    {
        Game restored(renderer,data,temporary);
        restored.readOnly();
        check(restored.health==80 && restored.armed && restored.sightOn &&
            std::abs(restored.battery-battery)<1e-7,"Delbert equipment and health survive reload");
        check(restored.wildlife(0,0).defeated[0] && restored.layer(0,expedition::Village).at(villageDoor)==OpenDoor,
            "defeated crawlers and village door changes survive reload");
    }
    std::filesystem::remove(temporary);
    const double oldSurface=std::pow(0.25,mobility::PreviousSurfaceZoom);
    const double oldRealm=std::pow(0.42,mobility::PreviousRealmZoom);
    combatEnabled=false;
    family=level=-1;
    feet={21.52,59.001,-13.25};
    zoom=goalZoom=mobility::PreviousSurfaceZoom;
    flying=true;smoothInput={};vertical.reset();grounded=false;
    world.stream(floorCell(feet),2);
    wheel(100);
    for(int i=0;i<45;i++)update(1.0/60,{});
    check(std::abs(body()-oldSurface*std::pow(0.25,mobility::ExtraSteps*mobility::SurfaceStep))<1e-12 && precision==FineDepth,
        "surface scroll reaches ten more detents and exposes the eighth detail tier");
    const Vec start=feet;
    for(int i=0;i<60;i++)update(1.0/60,{1,0,0,false,false,false});
    const double walking=length(feet-start);
    feet=start;smoothInput={};
    for(int i=0;i<60;i++)update(1.0/60,{1,0,0,false,false,true});
    check(length(feet-start)>0.012 && length(feet-start)>6*walking && !overlapsField(feet,body()),
        "Ctrl restores brisk traversal at the smallest scale");
    feet=start;smoothInput={};
    const double surfaceCell=1.0/subdivisions(FineDepth);
    const Cell surfaceSeed=floorCell((camera()+Vec{0.00009,0,0})/surfaceCell);
    check(world.apply({FineDepth,surfaceSeed,Stone})==1,"plant smallest surface cell");
    aim((surfaceSeed.v()+Vec{0.5,0.5,0.5})*surfaceCell);
    const auto surfaceBalance=bag[Stone];
    armed=false;
    key(I);
    check(!world.sample(FineDepth,surfaceSeed).amount && bag[Stone]==surfaceBalance+FullBlock,
        "mine the eighth-tier surface target for a whole resource");

    family=1;level=micro::Count-1;
    innerZoom=innerGoal=mobility::PreviousRealmZoom;
    innerFeet={24.53,5.04,7.75};smoothInput={};vertical.reset();
    wheel(100);
    for(int i=0;i<45;i++)update(1.0/60,{});
    check(std::abs(innerSize()-oldRealm*std::pow(0.42,mobility::ExtraSteps*mobility::RealmStep))<1e-12 && realmTier()==6,
        "hidden realm scroll reaches ten more detents and exposes the sixth detail tier");
    const Vec origin=innerFeet;
    for(int i=0;i<60;i++)update(1.0/60,{0,0,1,false,false,true});
    check(innerFeet.y-origin.y>1.5 && !layer(family,level).overlaps(innerFeet,innerSize()),
        "tiny realm flight traverses an entire cell in one second");
    innerFeet=origin;smoothInput={};
    const double realmCell=1.0/subdivisions(6);
    const Cell grain=floorCell((camera()+Vec{0.0018,0,0})/realmCell);
    auto& realm=layer(family,level);
    check(realm.setDetail(6,grain,Quartz),"place a fine quartz grain in a realm");
    aim((grain.v()+Vec{0.5,0.5,0.5})*realmCell);
    const auto realmBalance=bag[Quartz];
    key(I);
    check(!realm.blocks().sample(6,grain).amount && bag[Quartz]==realmBalance+FullBlock,
        "realm selection mines a tiny grain without removing the entire parent");
    const Cell rest=grain+Cell{0,3,0};
    check(realm.setDetail(6,rest,Quartz),"place a persistent tiny grain");
    saveFile=temporary;save();saveFile.clear();
    {
        Game restored(renderer,data,temporary);
        restored.readOnly();
        check(restored.level==micro::Count-1 && std::abs(restored.innerSize()-oldRealm*std::pow(0.42,mobility::ExtraSteps*mobility::RealmStep))<1e-10 &&
            restored.layer(1,micro::Count-1).blocks().sample(6,rest).id==Quartz,
            "reload the smallest scale and preserve real tiny realm placement");
    }
    std::filesystem::remove(temporary);
    level=family=-1;
    feet={0.5,62.1,0.5};zoom=goalZoom=0;precision=0;flying=false;
    world.stream(floorCell(feet),2);
    witness={};witness.active=true;witness.family=family;witness.level=level;
    witness.feet=feet+Vec{14,0,0};witness.heading=-Pi/2;
    yaw=Pi/2;pitch=0;
    const auto firstSeen=witness.feet;
    for(int i=0;i<72;i++){
        const Vec previous=witness.feet;
        updateWitness(1.0/60);
        check(witness.observed && length(witness.feet-previous)>0.001,
              "watching the open-ground stalk never suspends its stride");
    }
    check(length(witness.feet-firstSeen)>0.5 && health==80,"god. stalks continuously in direct view without harming Delbert");
    combatEnabled=true;witness.stareTime=0;witnessCueCooldown=0;
    for(int i=0;i<6;i++)updateWitness(0.1);
    check(witness.observed && witness.stareTime>0.59 && witnessCueCooldown>16,
        "sustained sight triggers the witness audio once");
    const double cooldown=witnessCueCooldown;
    const Vec whileWatching=witness.feet;
    for(int i=0;i<6;i++)updateWitness(0.1);
    check(witnessCueCooldown<cooldown && length(witness.feet-whileWatching)>0.3,"continued sight keeps the crawler moving without retriggering the cue");
    combatEnabled=false;
    key(Tab);
    check(inventory && !wantsCapture(),"Tab opens inventory and releases the pointer");
    key(Tab);
    check(!inventory && wantsCapture(),"Tab closes inventory");
    key(F);
    check(flashlight,"F toggles the flashlight");
    key(F);
    check(!flashlight,"F turns the flashlight off");
    const Vec atShot=witness.feet;
    aim(witness.rig.frame.point(witness.rig.pose.chest));armed=true;
    const auto ammo=bag[PulseRounds];
    button(1);
    const auto& absorbed=bolts[(nextBolt+bolts.size()-1)%bolts.size()];
    check(absorbed.absorbed && length(absorbed.to-camera())<15 && bag[PulseRounds]==ammo &&
        witness.feet==atShot,"god. absorbs a fired round without reacting");
    armed=false;
    yaw=-Pi/2;pitch=0;
    for(int i=0;i<24;i++)updateWitness(0.10);
    const double separation=length(witness.feet-feet);
    check(witness.feet!=atShot && separation>=10 && separation<=20,"god. relocates while out of view");
    zoom=goalZoom=0.5;precision=mobility::surfaceDetail(zoom);updateWitness(0.1);
    check(witness.scale==body(),"god. scales with Delbert");
    {
        const auto saved=witness;
        const auto savedPrecision=precision;
        witness.feet={39.5,61.000001,39.5};
        witness.scale=1;witness.heading=0;precision=1;
        world.stream(floorCell(witness.feet),2);
        const std::array<Vec,4> stance={{{-.35,0,-.89},{.35,0,-.89},{-.68,0,1.53},{.68,0,1.53}}};
        for(const auto offset:stance){
            const auto point=witness.feet+offset;
            world.apply({0,floorCell({point.x,60,point.z}),Stone});
        }
        const Cell raised=floorCell((witness.feet+stance[2])*4);
        world.apply({1,raised,Stone});
        witness.crouched=true;witness.climbing=witness.falling=witness.rig.ready=false;
        updateWitnessRig(0);
        for(int i=0;i<4;i++){
            const auto& limb=witness.rig.limbs[i];
            check(limb.phase==contact::Phase::Stance && std::abs(limb.support.point.y-(i==2?61.25:61))<0.001,
                "all four crouched limbs plant independently on an uneven ledge");
        }
        world.apply({1,raised,Air});
        updateWitnessRig(1.0/60);
        const auto& hand=witness.rig.limbs[2];
        check(hand.phase!=contact::Phase::Stance || hand.support.point.y<61.001,
            "a removed handhold releases on the next update");
        witness=saved;precision=savedPrecision;refreshWitnessMesh(0);
        world.stream(floorCell(feet),2);
    }
    zoom=goalZoom=0;precision=0;
    witness.feet=feet+Vec{26,0,0};witness.scale=1;
    yaw=Pi/2;pitch=0;
    const auto distant=witness.feet;
    updateWitness(0.1);
    check(length(witness.feet-distant)>0 && length(witness.feet-distant)<0.6 && witness.step==1,
        "god. walks visibly when outside stalking range");
    witness.feet=feet+Vec{2,0,0};witness.wait=0;yaw=-Pi/2;
    updateWitness(0.1);
    check(length(witness.feet-feet)>=10,"god. can relocate after Delbert comes within three blocks");

    feet={68.5,59.001,79.5};yaw=-Pi/2;
    world.stream(floorCell(feet),2);
    for(int z=74;z<=84;z++)for(int x=35;x<=47;x++){
        world.apply({0,{x,58,z},Stone});
        if(x>=40)for(int y=59;y<=61;y++)world.apply({0,{x,y,z},Stone});
    }
    witness.feet={38.94,59.001,79.5};witness.heading=Pi/2;witness.climbing=false;
    bool climbed=false,crested=false;
    for(int frame=0;frame<160 && !crested;frame++){
        const auto before=witness.feet;
        updateWitness(1.0/60);
        check(length(witness.feet-before)<0.33,"climbing advances smoothly without teleporting");
        check(!world.overlaps(witness.feet,4.15,2.25/4.15),"climber keeps its hull outside the wall");
        climbed|=witness.climbing && witness.feet.y>59.5;
        crested=climbed && !witness.climbing && witness.feet.x>39.8 && witness.feet.y>=61.999;
    }
    check(climbed && crested,"god. scales a three-block wall and steps onto its top");

    {
        const auto saved=witness;
        witness.feet={42,72,79};witness.heading=0;witness.scale=1;
        witness.crouched=witness.climbing=witness.rig.ready=false;witness.falling=true;
        updateWitnessRig(0);
        const auto origin=witness.feet+Vec{0,0.25,2};
        check(witnessHit(origin,{0,0,-1},4)==4,"shots pass through the space between the legs");

        witness.feet={38.94,59.001,79.5};witness.heading=Pi/2;
        witness.climbing=witness.crouched=true;witness.climbDirection={1,0,0};
        witness.falling=witness.rig.ready=false;
        updateWitnessRig(0);
        const auto& palm=witness.rig.limbs[2];
        check(palm.phase==contact::Phase::Stance,"the raised palm finds the wall");
        const auto start=palm.support.point+palm.support.normal*0.16;
        const auto ray=palm.support.normal*-1;
        check(witnessHit(start,ray,0.20)<0.20,"the rendered palm absorbs a shot at its wall contact");
        witness.feet.z+=2;witness.rig.ready=false;
        updateWitnessRig(0);
        check(witnessHit(start,ray,0.20)==0.20,"the prior palm position clears when the body moves");
        witness=saved;refreshWitnessMesh(0);
    }

    const Cell powerSource{12,58,12},powerLamp{13,58,12};
    auto savedDevices=std::exchange(devices,{});
    const bool oldLegacy=legacyRig;
    legacyRig=false;night=false;
    devices[powerLamp]=Device{Lamp};
    devices[powerSource]=Device{CrankGenerator};
    refreshGrid();
    const double crank=lightAt(powerLamp);
    devices[powerSource].id=SolarPanel;refreshGrid();
    check(crank>0 && std::abs(lightAt(powerLamp)-crank*2)<1e-8,"solar provides twice the crank output in daylight");
    night=true;refreshGrid();
    check(lightAt(powerLamp)==0,"solar stops generating at night");
    night=false;legacyRig=oldLegacy;devices=std::move(savedDevices);refreshGrid();

    feet={111.5,62.0002,95.5};zoom=goalZoom=0;precision=0;
    world.stream(floorCell(feet),2);
    world.apply({0,{111,61,95},Stone});
    world.apply({0,{111,63,95},Stone});
    check(world.overlaps(feet,body(),PlayerHeight) && !world.overlaps(feet,body(),CrouchHeight),
        "standing and crouched Delbert hulls have their intended heights");
    stance(true);stance(false);
    check(crouched,"releasing crouch under a low ceiling keeps the smaller hull");
    world.apply({0,{111,63,95},Air});stance(false);
    check(!crouched,"Delbert stands after clearing the ceiling");

    const Cell floor{113,62,95};
    world.apply({0,floor,CampTile});
    world.apply({0,{112,61,95},Stone});
    aim(floor.v()+Vec{0.25,0.55,0.5});
    bag[IronPick]=0;button(1);
    check(world.sample(0,floor).id==CampTile,"ordinary mining preserves camp flooring");
    bag[IronPick]=FullBlock;const auto campBalance=bag[CampTile];key(I);
    check(!world.sample(0,floor).amount && bag[CampTile]==campBalance+FullBlock,"iron pick recovers camp flooring");
    const auto oldGates=gates;
    gates[0]=Vec{114.5,62,95.5};
    aim(*gates[0]+Vec{0,1.19,0});
    button(1);
    check(!gates[0] && bag[MeadowGate]==FullBlock,"iron pick recovers a portal into the build inventory");
    world.apply({0,{113,61,95},Stone});
    selectMaterial(MeadowGate);aim({113.47,61.99,95.55});button(3);
    check(gates[0] && floorCell(*gates[0])==Cell{113,62,95} && !bag[MeadowGate],
        "portal can be mounted on another supported surface");
    saveFile=temporary;save();saveFile.clear();
    {
        Game restored(renderer,data,temporary);
        restored.readOnly();
        check(restored.gates==gates,"relocated portal coordinates survive save and reload");
    }
    std::filesystem::remove(temporary);gates=oldGates;

    input::Bindings remapped(temporary);
    check(remapped.assign(0,platform::I) && remapped.physical(W)==I && remapped.physical(I)==W && remapped.save(),
        "binding conflict swaps controls and saves the change");
    const input::Bindings reopened(temporary);
    check(reopened.physical(W)==I && reopened.logical(W)==I,"controls persist across sessions");
    std::filesystem::remove(temporary);
    {
        std::ofstream blocked(temporary);
        blocked<<"original test data";
    }
    saveFile=temporary/"world.save";
    paused=true;enableWorldPicker("Test world");
    key(W);
    check(paused && !requestsWorldPicker(),"a failed save keeps the current world open");
    saveFile.clear();
    std::filesystem::remove(temporary);
    std::cout<<"Verified tutorial, scale transitions, v0.7.0 shrink range, tiny movement and mining, layered saves, workshop, combat, encounter behavior and bindings.\n";
}

micro::Area& Game::layer(int set,int depth){
    auto& area=nests[set][depth];
    if(!area)area=std::make_unique<micro::Area>(set,depth,world.seed,modernWorld);
    return *area;
}
int Game::entrance() const {
    for(int i=0;i<int(gates.size());i++)if(gates[i] && length(feet-*gates[i])<2.7)return i;
    return -1;
}
void Game::enterSurfaceFortress(){
    if(!modernWorld || level>=0 || zoom!=FineDepth || goalZoom!=FineDepth)return;
    surfaceYaw=yaw;surfacePitch=pitch;
    // Keep the surface feet as the return anchor. The existing local-coordinate
    // room represents this microscopic tier, avoiding tiny world-space meshes.
    surfaceFortress=true;family=0;level=micro::Fortress;
    innerFeet=micro::Arrival;yaw=Pi;pitch=0;
    innerZoom=innerGoal=0;
    vertical.reset();grounded=false;smoothInput={};previousJump=false;portalZoom=false;
    for(auto& bolt:bolts)bolt.age=1;
    layer(family,level);
    message("Final shrink tier: The Marrowkeep");
}
bool Game::descend(bool shortcut){
    const int gate=level<0?entrance():family;
    const int child=level+1;
    if(gate<0 || child>=micro::Count)return false;
    if(level>=0 && length(innerFeet-micro::Descent)>3.1)return false;
    if(level<0 && (shortcut?zoom<0.75:zoom<2.6 || goalZoom<2.6))return false;
    if(level>=0 && (shortcut?innerZoom<0.28:innerZoom<0.72 || innerGoal<0.72))return false;
    if(!ownsWeapon()){
        if(shortcut || statusTime<=0)message("Carry a pistol or rifle to enter a portal");
        return shortcut;
    }
    if(!shortcut && !modernWorld){
        const double matched=std::log(micro::parentScale(child))/std::log(level<0?0.25:0.42);
        double& localGoal=level<0?goalZoom:innerGoal;
        const double localZoom=level<0?zoom:innerZoom;
        if(localZoom+0.018<matched){
            localGoal=std::max(localGoal,matched);
            portalZoom=true;
            return false;
        }
    }
    if(level<0){surfaceYaw=yaw;surfacePitch=pitch;}
    else rope[level]={innerFeet,yaw,pitch,innerZoom};
    family=gate;level=child;
    innerFeet=micro::Arrival;
    innerZoom=innerGoal=0;
    vertical.reset();grounded=false;smoothInput={};previousJump=false;portalZoom=false;
    for(auto& bolt:bolts)bolt.age=1;
    layer(family,level);
    message(std::string("Entered ")+micro::details(level).name);
    return true;
}
void Game::ascend(){
    if(level<0)return;
    const int previous=level;
    if(inFortress() && surfaceFortress){
        surfaceFortress=false;level=family=-1;yaw=surfaceYaw;pitch=surfacePitch;
        // Start at the original tiny hull and let ordinary collision-aware
        // growth decide whether the next tier fits at the saved anchor.
        zoom=FineDepth;goalZoom=FineDepth-1;precision=FineDepth;
        vertical.reset();grounded=false;smoothInput={};previousJump=false;portalZoom=false;
        for(auto& bolt:bolts)bolt.age=1;
        world.stream(floorCell(feet),2);recoverPosition();focus();
        message("Returned to the surface");return;
    }
    if(inFortress()){
        level=micro::Count-1;innerFeet=rope[level].feet;yaw=rope[level].yaw;pitch=rope[level].pitch;
        innerZoom=innerGoal=5;vertical.reset();grounded=false;smoothInput={};previousJump=false;portalZoom=false;
        for(auto& bolt:bolts)bolt.age=1;
        recoverPosition();focus();message("Returned to Atomic Grove");return;
    }
    const double scale=innerSize()*micro::parentScale(previous);
    const Vec parentPoint=micro::toParent(family,previous,innerFeet)+
        (previous==0?gateAnchor(family)-micro::Entrances[family]:Vec{});
    level--;
    vertical.reset();grounded=false;smoothInput={};previousJump=false;portalZoom=false;
    for(auto& bolt:bolts)bolt.age=1;
    if(level<0){
        feet=parentPoint;
        family=-1;
        zoom=std::clamp(std::log(scale)/std::log(0.25),0.0,mobility::SurfaceZoom);
        if(modernWorld)zoom=std::clamp(std::round(zoom),0.0,double(FineDepth));
        goalZoom=modernWorld?0:0.55;
        precision=surfaceTier();
        world.stream(floorCell(feet),2);
        recoverPosition();
        message("Returned to the surface");
    }else{
        auto& room=layer(family,level);
        innerZoom=std::clamp(std::log(scale)/std::log(modernWorld?0.25:0.42),0.0,mobility::RealmZoom);
        if(modernWorld)innerZoom=std::clamp(std::round(innerZoom),0.0,6.0);
        innerFeet=parentPoint;
        if(room.overlaps(innerFeet,innerSize(),hullHeight())){
            const Vec anchored=micro::Descent+Vec{0,0.08,0};
            innerFeet=room.overlaps(anchored,innerSize(),hullHeight())?rope[level].feet:anchored;
        }
        innerGoal=modernWorld?0:std::min(0.46,rope[level].zoom);
        recoverPosition();
        message(std::string("Rose out of ")+micro::details(previous).name);
    }
    focus();
}

void Game::moveMicro(double dt,const Motion& controls){
    auto& region=layer(family,level);
    if(region.overlaps(innerFeet,innerSize(),hullHeight()))recoverPosition();
    const double request=std::lerp(innerZoom,innerGoal,1-std::exp(-11*dt));
    const double wanted=modernWorld?innerGoal:std::abs(request-innerGoal)<0.002?innerGoal:request;
    const auto fits=[&](double value){return !region.overlaps(innerFeet,std::pow(modernWorld?0.25:0.42,value),hullHeight());};
    if(modernWorld)innerZoom=mobility::fitTier(int(innerZoom),int(innerGoal),fits);
    else innerZoom=fitZoom(innerZoom,wanted,fits);
    if(wanted<innerZoom-1e-5 && statusTime<=0)message("Move clear of the material to grow");
    const double size=innerSize();
    const Vec ahead{std::sin(yaw),0,-std::cos(yaw)};
    const Vec right{std::cos(yaw),0,std::sin(yaw)};
    Vec wish=ahead*controls.ahead+right*controls.across;
    if(flying)wish.y=controls.rise;
    moveInput(dt,wish);
    const auto speed=modernWorld?mobility::scaledSpeed:mobility::speed;
    const Vec move=smoothInput*(dt*speed(size,true,flying,controls.crouch,controls.brisk));
    const auto body=World::playerBox(innerFeet,size,hullHeight());
    constexpr double edge=0.015;
    if((move.x<0 && body.lo.x<1+edge) || (move.x>0 && body.hi.x>micro::Width-1-edge) ||
       (move.z<0 && body.lo.z<1+edge && innerFeet.z<micro::Arrival.z-1.7) ||
       (move.z>0 && body.hi.z>micro::Depth-1-edge) ||
       (move.y>0 && body.hi.y>micro::Height-1-edge)){
        ascend();return;
    }
    const int detail=realmTier();
    const double step=std::clamp(1.0001/subdivisions(detail),size*0.32,size*1.35);
    mobility::advance(innerFeet,size,move,dt,step,modernWorld?size:std::max(size,0.045),flying,controls.jump,
        previousJump,grounded,vertical,[&](Vec p,double hull,int axis,double distance){
            return region.sweep(p,hull,axis,distance,hullHeight());
        });
    descend(false);
}

void Game::drawMicro(){
    auto& region=layer(family,level);
    if(meshFamily!=family || meshLevel!=level){
        for(auto& tile:realmTiles)tile.revision=~std::uint64_t{};
        for(auto& cache:realmDetails)cache.valid=false;
        meshFamily=family;meshLevel=level;
    }
    const auto& info=micro::details(level);
    const auto eye=camera(),dir=viewDirection(yaw,pitch),right=unit(cross(dir,{0,1,0}));
    View view{eye,dir,right,cross(right,dir)};
    view.ambient=modernWorld?(level==0?.76f:.80f):(level>=4?.84f:.86f);
    // The room uses a stable local light direction rather than the surface
    // world's day/night clock. Only local terrain casts these shadows.
    view.forestLighting=modernWorld;view.shadowWeight=.78f;
    view.sun=view.shadowSun=unit({.38,.82,.43});
    view.sky=info.sky;view.near=0.025*innerSize();view.fog=inFortress()?75:level==6?53:82;
    if(modernWorld && level==0)view.sky={.29f,.39f,.32f};
    view.detailBody=innerSize();
    const double parentRatio=1/micro::parentScale(level);
    view.far=inFortress()?160.:std::max(160.0,parentRatio*20);
    std::array<Light,8> lights={{{micro::Descent+Vec{0,1,0},1.6f},{micro::Arrival+Vec{0,1,-1.7},1.3f}}};
    if(level==expedition::Village){
        lights[2]={{19.6,7,20.5},1.22f};
        lights[3]={{28.4,7,20.5},1.22f};
    }
    if(inFortress()){view.ambient=.68f;lights[0].strength=0;for(int i=0;i<2;i++)lights[2+i]={settlement::Campfires[i]+Vec{0,.85,0},float(2.+.15*std::sin(broods[family].clock*5+i*2))};lights[4]={settlement::BroodCradle+Vec{0,1.4,0},1.2f};}
    view.torchRange=flashlight?float(25*innerSize()):0;
    flashLight(lights);
    if(surfaceFortress)lights[1].strength=0;
    view.effectTime=playTime;view.gateLightCount=surfaceFortress?0:inFortress()?1:2;
    view.gateLights={micro::Arrival+Vec{0,1,-2},micro::Descent+Vec{0,1,0}};
    renderer.start(view,fieldWidth,fieldHeight,lights);
    if(sceneryFamily!=family || sceneryLevel!=level)makeParentScenery();
    const int local=realmTier();
    std::array<Cut,FineDepth> cuts{};
    const int visibleDepth=prepareMatterDetails(region.blocks(),realmDetails,eye,local,
        std::abs(innerGoal-innerZoom)>0.10?1:2,cuts);
    const Cut* closeCut=visibleDepth?&cuts[0]:nullptr;
    const double aspect=double(fieldWidth)/fieldHeight;
    struct NearbyTile {int index;double distance;};
    std::array<NearbyTile,micro::TileCount> visible;
    int count=0;
    for(int index=0;index<micro::TileCount;index++){
        const auto portion=micro::tileBounds(index);
        const Box bounds{portion.lo.v()-Vec{0.25,0.25,0.25},portion.hi.v()+Vec{0.25,0.25,0.25}};
        if(!view.sees(bounds,aspect,view.fog+4))continue;
        const auto dx=std::max({bounds.lo.x-eye.x,eye.x-bounds.hi.x,0.0});
        const auto dz=std::max({bounds.lo.z-eye.z,eye.z-bounds.hi.z,0.0});
        visible[count++]={index,std::hypot(dx,dz)};
    }
    std::sort(visible.begin(),visible.begin()+count,[](auto a,auto b){return a.distance<b.distance;});
    int remaining=modernWorld?4:micro::TileCount;
    for(int i=0;i<count;i++){
        const int index=visible[i].index;
        auto& tile=realmTiles[index];
        const auto revision=region.tileVersion(index);
        if(tile.revision!=revision){
            // Collision reads the live matter immediately. Only initially unseen
            // tiles wait for a budget slot after the player edits nearby terrain.
            if(tile.revision==~std::uint64_t{} && remaining<=0)continue;
            --remaining;
            const auto portion=micro::tileBounds(index);
            micro::TerrainMeshes geometry;
            region.drawMeshes(geometry,portion);
            tile.solid.uploadTerrain(geometry.solid);tile.close.upload(geometry.close);tile.distant.upload(geometry.distant);
            tile.fixtures.upload(geometry.fixtures);tile.fixtureProxy.upload(geometry.fixtureProxy);
            tile.exact.uploadTerrain(geometry.exact);
            tile.roof.upload(geometry.roof);tile.roofProxy.upload(geometry.roofProxy);
            tile.trees.upload(geometry.trees);tile.trees.terrainSurface=true;tile.sprigs.upload(geometry.sprigs);
            tile.sprigs.terrainSurface=true;tile.sprigs.twoSidedLeaf=true;
            if(tile.roofProxy.vertices)tile.roofBounds=geometryBounds(geometry.roofProxy);
            tile.revision=revision;
        }
    }
    if(modernWorld && renderer.graphics().shadows>0){
        std::uint64_t revision=0x5245414c4d000000ull+std::uint64_t(family*micro::Count+level);
        for(int i=0;i<micro::TileCount;i++)if(realmTiles[i].revision!=~std::uint64_t{})
            revision+=std::uint64_t(mix(std::uint32_t(i)^(std::uint32_t(realmTiles[i].revision)*173u+131u)))*0x9e3779b97f4a7c15ull;
        if(renderer.beginCanopy(revision)){
            for(int i=0;i<micro::TileCount;i++){
                const auto& tile=realmTiles[i];if(tile.revision==~std::uint64_t{})continue;
                const auto origin=micro::tileBounds(i).lo.v();
                renderer.shadowTree(tile.solid,{});renderer.shadowTree(tile.exact,origin);
                renderer.shadowTree(tile.trees,origin);renderer.shadowTree(tile.roof,{});
            }
            renderer.endCanopy();
        }
        if(witness.active && witness.rig.ready && witness.family==family && witness.level==level &&
            length(witness.feet-eye)<42*witness.scale && renderer.beginCreatureShadow(witness.feet+Vec{0,2,0}*witness.scale,7*witness.scale)){
            witnessBody.upload(true);
            for(const auto& part:witnessPose())renderer.shadowCreature(part.mesh->render,part.origin,witness.scale,part.heading,part.tilt);
            renderer.endCreatureShadow();
        }
    }
    renderer.faceCulling(true);
    for(int i=0;i<count;i++){
        const auto& tile=realmTiles[visible[i].index];
        if(tile.revision==~std::uint64_t{})continue;
        renderer.draw(tile.solid,{},1,0,closeCut);
        const auto origin=micro::tileBounds(visible[i].index).lo.v();
        renderer.draw(tile.exact,origin);renderer.draw(tile.trees,origin);
        if(tile.roofProxy.vertices){
            const auto& box=tile.roofBounds;
            const Vec gap{std::max({box.lo.x-eye.x,eye.x-box.hi.x,0.0}),std::max({box.lo.y-eye.y,eye.y-box.hi.y,0.0}),
                std::max({box.lo.z-eye.z,eye.z-box.hi.z,0.0})};
            const bool near=length(gap)<7.2;
            renderer.draw(near?tile.roof:tile.roofProxy,{},1,0,closeCut,0,0,near?0:0.35f);
        }
    }
    renderer.faceCulling(false);
    for(int i=0;i<count;i++){
        const auto& tile=realmTiles[visible[i].index];
        if(tile.revision==~std::uint64_t{})continue;
        const bool nearFixtures=visible[i].distance<(innerZoom>0.75?4.0:10.0);
        renderer.draw(nearFixtures?tile.fixtures:tile.fixtureProxy);
        if(renderer.graphics().foliage>0 && (visible[i].distance<17 || renderer.graphics().foliage>=3))
            renderer.draw(tile.sprigs,micro::tileBounds(visible[i].index).lo.v());
        if(level==micro::Count-1){
            const bool near=visible[i].distance<(innerZoom>0.75?4:8);
            renderer.draw(near?tile.close:tile.distant,{},1,0,closeCut,0,0,near?0:1.2f);
        }
    }
    drawMatterDetails(realmDetails,visibleDepth,cuts);
    if(!surfaceFortress)drawGate(micro::Arrival-Vec{0,0,2},1);
    if(!inFortress())drawGate(micro::Descent,1.3,level==micro::Count-1);
    if(inFortress())drawFortress(view);
    drawExpedition(view,aspect);
    drawGoblin(view);
    drawWitness();
    drawHorror(view);
    renderer.distanceFog(modernWorld?42.0:std::max(130.0,parentRatio*13));
    renderer.faceCulling(true);
    renderer.draw(parentScenery,{},1,0,nullptr,0,0,1.45f);
    renderer.faceCulling(false);
    renderer.distanceFog(view.fog);
    if(target && (local<=2 || furniture(target->id))){
        const int depth=furniture(target->id)?0:local;
        const auto block=parent(target->cell,subdivisions(target->level-depth));
        drawTerrainSelection(region.blocks(),block,depth,target->id,0.92f);
    }
}

void Game::saveMicro(std::ostream& out) const {
    out<<"flight "<<flying<<"\n";
    if(level>=0){
        out<<"micro "<<family<<" "<<level<<" "<<innerFeet.x<<" "<<innerFeet.y<<" "<<innerFeet.z<<" "
            <<yaw<<" "<<pitch<<" "<<innerZoom<<"\n";
        if(surfaceFortress)out<<"surfacefortress 1\n";
        for(int depth=0;!surfaceFortress && depth<level;depth++){
            const auto& p=rope[depth];
            out<<"rope "<<depth<<" "<<p.feet.x<<" "<<p.feet.y<<" "<<p.feet.z<<" "
                <<p.yaw<<" "<<p.pitch<<" "<<p.zoom<<"\n";
        }
    }
    for(int group=0;group<2;group++)for(int depth=0;depth<micro::Total;depth++){
        if(!nests[group][depth])continue;
        for(const auto& [pos,id]:nests[group][depth]->edits)
            out<<"tiny "<<group<<" "<<depth<<" "<<pos.x<<" "<<pos.y<<" "<<pos.z<<" "<<id<<"\n";
        for(const auto& [chunk,edits]:nests[group][depth]->blocks().saveChunks()){
            for(const auto& edit:edits)if(edit.level>0){
                out<<"tinyfine "<<group<<" "<<depth<<" ";
                writeTerrainEdit(out,edit);
            }
        }
    }
}

void Game::verifyHorror(const std::filesystem::path& path){
    const auto check=[](bool condition,const char* text){if(!condition)throw std::runtime_error(text);};
    check(modernWorld && world.seed.has_value(),"Horror test needs a seeded world");
    // First exercise the actual forest population, not a hand-placed test
    // animal. The real terrain collision must support every resting deer.
    const auto seed=*world.seed;
    // Exercise normal, unmodified starter terrain before using the isolated
    // collision test platform. No artificial floor or relocated guards.
    const auto startingGuards=guards;const auto startingBag=bag;
    witness.active=false;level=family=-1;zoom=goalZoom=0;precision=0;
    feet=guardChest()+Vec{-2.8,0,2.0};world.stream(floorCell(feet),2);
    if(const auto* roof=world.roof(floorCell(feet)))feet.y=roof->height(feet.x-std::floor(feet.x),feet.z-std::floor(feet.z))+.01;
    std::array<double,3> initialGap{};
    for(int role=0;role<guardCount();role++)initialGap[role]=dread::horizontal(goblin(role).feet-guardChest());
    for(int frame=0;frame<160;frame++)updateGoblin(.05);
    for(int role=0;role<guardCount();role++){
        const auto& g=goblin(role);
        check(g.alert && g.health>0,"Generated surface guard did not react to the player");
        check(!world.overlaps(g.feet,settlement::GuardScale,1.44),"Generated guard remained buried in terrain");
        check(dread::horizontal(g.feet-guardChest())<initialGap[role]+.05,"Generated guard failed to approach chest");
    }
    const double finalGap=dread::horizontal(goblin().feet-guardChest());
    std::cout<<"Natural starter terrain: primary guard distance "<<initialGap[0]<<" -> "<<finalGap<<"; all three guardians reacted.\n";
    check(finalGap<2.1 && finalGap<initialGap[0]-.5,"Primary guard did not actually reach its chest on generated terrain");
    // Real terrain encounter: place the Witness on a supported, unobstructed
    // approach (no test floor), then let normal hunting and guard AI run.
    bool naturalHunt=false;
    for(int attempt=0;attempt<16 && !naturalHunt;attempt++){
        const auto readyGuards=guards;const auto readyBag=bag;
        const Vec home=goblin().feet;
        const double angle=attempt*2.3999632297;
        Vec point=home+Vec{std::sin(angle)*4.1,0,std::cos(angle)*4.1};
        const auto* roof=world.roof(floorCell(point));if(!roof)continue;
        point.y=roof->height(point.x-std::floor(point.x),point.z-std::floor(point.z))+1.3;
        if(world.overlaps(point,4.15,2.25/4.15))continue;
        const double drop=world.sweep(point,4.15,1,-2.8,2.25/4.15);if(drop<=-2.799)continue;
        point.y+=drop+.001;
        const Vec ray=home+Vec{0,1.25,0}-(point+Vec{0,1.77,0});
        if(witnessRay(point+Vec{0,1.77,0},unit(ray),length(ray)-.05))continue;
        witness={};witness.active=true;witness.crouched=true;witness.family=family;witness.level=level;
        witness.feet=point;witness.heading=std::atan2(home.x-point.x,home.z-point.z);updateWitnessRig(0);
        for(int frame=0;frame<300;frame++){
            updateGoblin(.05);updateWitness(.05);
            if(witness.horror.mode==dread::Mode::Feed && witness.horror.prey.kind==dread::Kind::Goblin){naturalHunt=true;break;}
        }
        guards=readyGuards;bag=readyBag;
    }
    check(naturalHunt,"No successful guardian hunt on unmodified generated terrain");
    std::cout<<"Natural starter terrain: guardian hunt reached feeding without a forced hunt state.\n";
    guards=startingGuards;bag=startingBag;witness={};learnedVoices.reset();rememberedVoice=-1;
    double trail=44;
    while(trail<100 && (!natural::trailLand(0,trail,seed) || !natural::trailLand(0,trail+5,seed)))trail+=2;
    const auto trailPoint=[&](double z){const double x=natural::trailX(0,z,seed);return Vec{x,natural::surfaceHeight(x,z,seed)+.01,z};};
    feet=trailPoint(trail);world.stream(floorCell(feet),2);
    if(const auto* roof=world.roof(floorCell(feet)))feet.y=roof->height(feet.x-std::floor(feet.x),feet.z-std::floor(feet.z))+.01;
    yaw=Pi;pitch=0;witness.active=false;
    for(int i=0;i<60;i++)updateForestLife(.05);
    check(forestDeer.size()<=1,"Rare wildlife cap exceeded");
    int supported=0;
    for(const auto& [id,deer]:forestDeer){
        if(deer.fallSpeed>.1)continue;
        check(world.sweep(deer.feet,1.5,1,-.15,1.2)>-.149,"A resting deer floated above the ground");++supported;
    }
    check(forestDeer.empty() || supported>0,"No grounded woodland animal");
    std::cout<<"Natural forest: "<<forestDeer.size()<<" resident animals, "<<supported<<" supported at rest.\n";
    forestDeer.clear();
    // Real collision and shot tests on an isolated high platform. No user
    // world is opened, and the artificial terrain is never distributed.
    level=family=-1;zoom=goalZoom=0;precision=0;feet={.5,86.001,.5};
    world.stream(floorCell(feet),2);
    for(int x=-15;x<=15;x++)for(int z=-5;z<=24;z++)world.apply({0,{x,85,z},Stone});
    witness={};witness.active=true;witness.family=family;witness.level=level;
    witness.feet={.5,86.001,6.5};witness.heading=Pi;witness.crouched=true;
    witness.sight.observe(camera(),0,1);
    for(int i=0;i<24;i++)updateWitnessRig(1./60);
    const auto aimAt=[&](Vec point){const Vec v=point-camera();yaw=std::atan2(v.x,-v.z);pitch=std::atan2(v.y,std::hypot(v.x,v.z));};
    aimAt(witness.rig.frame.point(witness.rig.pose.neck)+Vec{0,.12,0});
    check(witnessHit(camera(),viewDirection(yaw,pitch),25)<25,"Live mesh was not hittable");
    bag[PulseRounds]=6*FullBlock;armed=true;weapon=PulseRifle;
    const auto before=bag;const int hp=health;const Vec position=witness.feet;
    fire();
    check(bag[PulseRounds]==before[PulseRounds],"Witness failed to refund round");
    check(std::accumulate(bag.begin(),bag.end(),std::int64_t{})-std::accumulate(before.begin(),before.end(),std::int64_t{})==FullBlock,"Witness reward was not exactly one resource");
    auto& impact=bolts[(nextBolt+bolts.size()-1)%bolts.size()];
    check(impact.absorbed && witness.feet==position && health==hp,"Shot harmed or moved the Witness or player");
    impact.age=.10;std::array<Light,8> lights{};flashLight(lights);
    check(lights[0].strength>0,"No impact light during the flash");
    impact.age=.21;lights={};flashLight(lights);check(lights[0].strength==0,"Impact light persisted");
    const auto afterHit=bag;
    for(int x=-2;x<=2;x++)for(int y=86;y<=90;y++)world.apply({0,{x,y,3},Stone});
    fire();check(!bolts[(nextBolt+bolts.size()-1)%bolts.size()].absorbed && bag[PulseRounds]==afterHit[PulseRounds]-FullBlock,"Blocked shot awarded a refund");
    check(std::accumulate(afterHit.begin(),afterHit.end(),std::int64_t{})-std::accumulate(bag.begin(),bag.end(),std::int64_t{})==FullBlock,"Blocked shot awarded resources");
    for(int x=-2;x<=2;x++)for(int y=86;y<=90;y++)world.apply({0,{x,y,3},Air});
    pitch=1.54;const auto miss=bag[PulseRounds];fire();check(bag[PulseRounds]==miss-FullBlock,"Missed shot refunded");
    for(auto& shot:bolts)shot.age=1;
    armed=false;aimAt(witness.feet+Vec{0,1.5,0});
    // Place one real living surface resident in reach. The normal game AI,
    // not the test, must select it, catch it and transition to feeding.
    const Cell preyID{0,0,0};forestDeer.emplace(preyID,Deer{{.5,86.001,4.85},0,0,1});
    witness.horror.cooldown=0;witness.horror.scan=0;
    const auto resources=bag;
    for(int i=0;i<10 && witness.horror.mode!=dread::Mode::Feed;i++)updateWitness(1./60);
    check(witness.horror.mode==dread::Mode::Feed && consumedDeer.contains(preyID) && forestDeer.empty(),"AI did not consume the living prey");
    check(learnedVoices[4] && rememberedVoice==4,"Prey voice was not learned");
    double minimum=1e9;int overlapFrames=0;bool lifted=false;
    for(int i=0;i<int(dread::FeedDuration/.065)+5;i++){
        updateWitness(.065);
        if(witness.horror.mode!=dread::Mode::Feed)break;
        const auto& pose=witness.rig.pose;
        const std::array chains={pose.legs[0],pose.legs[1],pose.arms[0],pose.arms[1]};
        bool overlap=false;
        for(int a=0;a<4;a++)for(int b=a+1;b<4;b++){
            const double gap=contact::limbClearance(chains[a],a>=2,chains[b],b>=2);
            minimum=std::min(minimum,gap);overlap|=gap<-.005;
        }
        overlapFrames+=overlap;
        lifted|=!pose.arms[1].planted && pose.arms[1].tip.y>.55;
    }
    check(witness.horror.mode==dread::Mode::Watch && bag==resources && health==hp,"Feeding failed to finish harmlessly");
    check(lifted,"Feeding hand did not lift");
    std::cout<<"Live feeding limb minimum clearance: "<<minimum<<", overlapping frames: "<<overlapFrames<<"\n";
    check(overlapFrames==0,"Feeding introduced limb intersections");
    const auto originalGuard=goblin();
    auto& preyGuard=goblin();preyGuard.feet={.5,86.001,4.85};preyGuard.health=110;
    witness.feet={.5,86.001,6.5};witness.rig.ready=false;
    witness.horror.finish(0);witness.horror.scan=0;updateWitnessRig(0);
    for(int i=0;i<120 && witness.horror.mode!=dread::Mode::Feed;i++)updateWitness(1./60);
    check(witness.horror.mode==dread::Mode::Feed && witness.horror.prey.kind==dread::Kind::Goblin && preyGuard.health==0,
          "Witness did not hunt and defeat a real goblin");
    bool opened=false,closed=false,dropped=false;
    for(int i=0;i<int(dread::FeedDuration/.05)+5 && witness.horror.mode==dread::Mode::Feed;i++){
        updateWitness(.05);const auto parts=witnessPose();const double angle=parts[2].tilt-parts[1].tilt;
        opened|=angle<-.18;closed|=angle>.10;
        for(const auto& drop:witness.horror.droppings)dropped|=drop.ready;
    }
    check(opened && closed && dropped && health==hp && bag==resources,"Jaw/drop animation or non-hostility failed");
    std::cout<<"Goblin hunted and defeated; articulated jaw opened and closed; mouth debris reached terrain.\n";
    // Two surviving classes must retaliate after the first completed feeding.
    // Actual hit geometry is exercised through their timed attack state, not
    // through a synthetic inventory increment.
    const auto savedCrossbow=goblin(1),savedSword=goblin(2);
    const auto center=witness.feet;
    auto& crossbow=goblin(1);auto& sword=goblin(2);
    crossbow={};crossbow.ready=true;crossbow.feet=center+Vec{2.7,0,.7};
    sword={};sword.ready=true;sword.feet=center+Vec{-1.5,0,.8};
    crossbow.yaw=std::atan2(-2.7,-.7);sword.yaw=std::atan2(1.5,-.8);
    const auto beforeGuardHits=bag;
    for(int frame=0;frame<100;frame++){updateGoblin(.05);updateWitnessRig(.05);}
    check(crossbow.defending && sword.defending,"Surviving guardians did not retaliate together");
    check(bag[PulseRounds]>beforeGuardHits[PulseRounds] && witness.rage>0,"Guardian strikes did not return ammunition or provoke the witness");
    for(int i=0;i<ItemLimit;i++)if(i!=PulseRounds)check(bag[i]==beforeGuardHits[i],"Guardian strike incorrectly awarded a resource");
    check(health==hp,"Guardian strikes harmed the player");
    std::cout<<"Coordinated retaliation: "<<(bag[PulseRounds]-beforeGuardHits[PulseRounds])/FullBlock<<" rounds, no materials; rage="<<witness.rage<<".\n";
    bag=beforeGuardHits;crossbow=savedCrossbow;sword=savedSword;preyGuard=originalGuard;
    // Observe a nearby changed location, then inspect it once the player
    // has left. It does not consume any player-built matter.
    witness.horror.cooldown=0;witness.horror.scan=0;
    const Vec changed{-3,86.5,6.0};noticeWitness(changed);
    check(witness.horror.interesting(),"Visible activity was not remembered");
    updateWitness(.016);
    check(witness.horror.mode==dread::Mode::Inspect && witness.horror.goal==changed,"Curiosity failed to enter inspect mode");
    // Obstruction-aware retreat planning with a real stone wall and real
    // collision support. Travel itself uses the usual locomotion sweeps.
    witness.horror.finish(0);witness.horror.scan=0;witness.horror.interestKnown=false;
    witness.stareTime=1;
    for(int z=2;z<=9;z++)for(int y=86;y<=90;y++)world.apply({0,{4,y,z},Stone});
    const auto fits=[&](Vec p,double height){return !overlapsField(p,4.15,height/4.15);};
    const auto grounded=[&](Vec p){p.y=86.001;return p;};
    const auto blocked=[&](Vec a,Vec b){const Vec d=b-a;return length(d)>.03 && witnessRay(a,unit(d),length(d)-.025).has_value();};
    const Vec observer=camera();
    const auto head=witness.rig.frame.point(witness.rig.pose.neck);
    check(horrorPlan(.016,true,true,observer,head,fits,grounded,blocked) && witness.horror.mode==dread::Mode::Retreat,"No retreat behind actual wall found");
    const Vec goal=witness.horror.goal;
    check(blocked(observer,goal+Vec{0,1.05,0}) && blocked(observer,goal+Vec{0,1.85,0}),"Retreat destination was exposed");
    // Move through the actual sweeps, never warp the creature to the goal.
    bool concealed=false,returned=false;double maxStep=0;
    for(int i=0;i<410;i++){
        const Vec prior=witness.feet;updateWitness(.065);
        maxStep=std::max(maxStep,length(witness.feet-prior));
        concealed|=witness.horror.mode==dread::Mode::Concealed;
        returned|=witness.horror.mode==dread::Mode::Reappear;
        if(returned && witness.horror.mode==dread::Mode::Watch)break;
        if(!concealed && witness.horror.mode==dread::Mode::Watch)break;
    }
    std::cout<<"Retreat reached cover="<<concealed<<", returned="<<returned<<", max movement per frame="<<maxStep<<"\n";
    check(concealed && returned && maxStep<.8,"Retreat failed to walk into concealment and return");
    // One protected villager and the maximum single loss in this village.
    family=0;level=expedition::Village;
    dread::Prey protectedElf{dread::Kind::Elf,{},0},elf{dread::Kind::Elf,{},2},another{dread::Kind::Elf,{},1};
    check(!consumePrey(protectedElf) && consumePrey(elf) && !consumePrey(another),"Village protection failed");
    check(learnedVoices[2] && learnedVoices[4] && wildlife(family,level).missingElves.count()==1,"Multiple voices or elf persistence failed");
    level=family=-1;saveFile=path.string()+"-temporary.save";
    check(save(),"Horror save failed");
    consumedDeer.clear();learnedVoices.reset();rememberedVoice=-1;fauna[0][expedition::Village]->missingElves.reset();
    load();
    check(consumedDeer.contains(preyID) && learnedVoices[2] && learnedVoices[4] && fauna[0][expedition::Village]->missingElves[2],"Horror state failed to reload");
    std::filesystem::remove(saveFile);saveFile.clear();
    std::cout<<"Live mesh hit/refund, terrain-blocked shot, miss, flash expiry, consumption, curiosity, false retreat and persistent village/voice state passed.\n";

    // In-engine visual inspection on an unmodified natural trail, well away
    // from the artificial platform. The test positions the actor deliberately.
    bool visual=false;
    for(double z=trail;z<trail+32 && !visual;z+=2){
        if(!natural::trailLand(0,z,seed) || !natural::trailLand(0,z+6,seed))continue;
        world.stream(floorCell(trailPoint(z)),2);
        const auto snap=[&](Vec p,double width,double height)->std::optional<Vec>{
            const auto* roof=world.roof(floorCell(p));if(!roof)return {};
            p.y=roof->height(p.x-std::floor(p.x),p.z-std::floor(p.z))+1.3;
            if(world.overlaps(p,width,height))return {};
            const double drop=world.sweep(p,width,1,-2.8,height);
            if(drop<=-2.799)return {};
            return p+Vec{0,drop+.001,0};
        };
        auto p=snap(trailPoint(z),1,PlayerHeight),w=snap(trailPoint(z+5.4),4.15,2.25/4.15),d=snap(trailPoint(z+3.9),1.5,1.2);
        if(!p || !w || !d)continue;
        feet=*p;
        const Vec ray=*w+Vec{0,1.6,0}-camera();
        if(witnessRay(camera(),unit(ray),length(ray)-.1))continue;
        witness={};witness.active=true;witness.family=family;witness.level=level;witness.crouched=true;
        witness.feet=*w;witness.heading=std::atan2(feet.x-w->x,feet.z-w->z);
        witness.sight.observe(camera(),0,1);
        for(int i=0;i<30;i++)updateWitnessRig(1./60);
        aimAt(witness.rig.frame.point(witness.rig.pose.neck));
        forestDeer.clear();forestDeer.emplace(Cell{0,0,7},Deer{*d,witness.heading+Pi*.45,0,1});
        solarClock=solarcycle::CycleSeconds*.5;night=false;armed=false;flashlight=false;statusTime=0;
        for(int i=0;i<3;i++)draw(1280,720);
        capture(path.string()+"-wildlife.ppm");
        const auto oldFeet=feet; const auto oldYaw=yaw,oldPitch=pitch;
        const double deerYaw=forestDeer.begin()->second.heading;
        feet=*d+Vec{std::sin(deerYaw)*1.4+std::cos(deerYaw)*2.8,.15,std::cos(deerYaw)*1.4-std::sin(deerYaw)*2.8};
        aimAt(*d+Vec{0,1.08,0});
        for(int i=0;i<3;i++)draw(1280,720);
        capture(path.string()+"-deer-close.ppm");feet=oldFeet;yaw=oldYaw;pitch=oldPitch;
        witness.horror.cooldown=0;witness.horror.scan=0;
        for(int i=0;i<12 && witness.horror.mode!=dread::Mode::Feed;i++)updateWitness(.03);
        if(witness.horror.mode!=dread::Mode::Feed)continue;
        for(int i=0;i<85;i++){updateWitness(.05);
            if(i>8 && dread::biteProgress(witness.horror.age)>.65 && dread::biteProgress(witness.horror.age)<.95)break;
        }
        aimAt(witness.rig.frame.point(witness.rig.pose.chest));
        for(int i=0;i<3;i++)draw(1280,720);
        capture(path.string()+"-feeding.ppm");
        armed=true;weapon=PulseRifle;bag[PulseRounds]=6*FullBlock;
        aimAt(witness.rig.frame.point(witness.rig.pose.neck)+Vec{0,.12,0});fire();
        auto& shot=bolts[(nextBolt+bolts.size()-1)%bolts.size()];
        if(!shot.absorbed)continue;
        shot.age=.10;recoil=.1;
        for(int i=0;i<3;i++)draw(1280,720);
        capture(path.string()+"-flash.ppm");
        visual=true;
    }
    if(visual)std::cout<<"Natural trail feeding and flash render captured.\n";
    else std::cout<<"Natural trail visual setup did not find a valid pose; logic tests already passed.\n";
}

void Game::verifyFortress(const std::filesystem::path& path){
    readOnly();const auto check=[](bool ok,const char* what){if(!ok)throw std::runtime_error(what);};
    bag[PulseRifle]=FullBlock;bag[PulseRounds]=6*FullBlock;
    family=0;level=micro::Count-1;innerFeet=micro::Descent;innerZoom=innerGoal=6;
    check(!descend(true) && !nests[0][micro::Fortress],"Atomic Grove must not generate fortress");
    level=family=-1;zoom=goalZoom=0;precision=0;flying=true;
    const Vec anchor=feet;const double originalYaw=yaw,originalPitch=pitch;
    wheel(FineDepth-1);movement(1./60,{});
    check(level<0 && precision==FineDepth-1 && !nests[0][micro::Fortress],"Fortress generated before final shrink tier");
    wheel(1);movement(1./60,{});
    check(inFortress() && surfaceFortress && zoom==FineDepth && innerZoom==0 && yaw==Pi,"Ordinary shrinking did not generate fortress");
    check(length(feet-anchor)<1e-8 && surfaceYaw==originalYaw && surfacePitch==originalPitch,"Surface anchor was lost");
    flying=false;
    auto& room=layer(family,level);std::array<int,3> classes{};
    for(int i=0;i<guardCount();i++){
        auto& g=goblin(i);classes[guardClass(i)]++;
        check(!room.overlaps(g.feet,settlement::GuardScale,1.44),"Guard spawn obstructed");
        check(room.sweep(g.feet,settlement::GuardScale,1,-.1,1.44)>-.099,"Guard spawn unsupported");
    }
    check(classes==std::array<int,3>{4,6,5},"Garrison count incorrect");
    for(double z=5;z<38;z+=.25)check(!room.overlaps({24.5,5.01,z},1.),"Fortress approach obstructed");
    std::cout<<"Final surface shrink restriction, 15 supported guards and walkable entrance: PASS"<<std::endl;
    const auto image=[&](const char* name,Vec pos,double y,double p){innerFeet=pos;yaw=y;pitch=p;for(int i=0;i<10;i++)draw(960,540);capture(path.string()+name+".ppm");};
    image("-overview",{24.5,16,5},Pi,-.32);image("-courtyard",{24.5,5.01,17.5},Pi,.08);
    auto& g=goblin(4);g.health=0;g.visualClock=dread::FallDuration;
    auto& g2=goblin(5);g2.health=0;g2.visualClock=dread::FallDuration;
    updateBrood(0);check(broods[0].slot==4,"Brood did not select first vacancy");
    updateBrood(95);image("-brood",{24.5,5.01,35.5},Pi,.02);
    updateBrood(24.5);check(g.health==0 && g2.health==0,"Premature brood completion");
    innerFeet=micro::Arrival;updateBrood(.5);check(g.health==110 && g2.health==0 && broods[0].slot<0,"Brood did not replace exactly one guard");
    updateBrood(0);check(broods[0].slot==5,"Second vacancy not queued");
    updateBrood(23);std::cout<<"120-second sequential regrowth: PASS"<<std::endl;
    innerFeet={24.5,5.01,19};witness={};witnessHealth=200;witness.active=true;witness.family=family;witness.level=level;
    witness.feet={24.5,5.01,24.5};witness.heading=Pi;witness.crouched=true;
    for(int i=0;i<24;i++)updateWitnessRig(1./60);
    const Vec delta=witness.rig.frame.point(witness.rig.pose.neck)+Vec{0,.12,0}-camera();
    yaw=std::atan2(delta.x,-delta.z);pitch=std::atan2(delta.y,std::hypot(delta.x,delta.z));
    check(witnessHit(camera(),viewDirection(yaw,pitch),25)<25,"Fortress Witness not hittable");
    armed=true;weapon=PulseRifle;const auto ammo=bag[PulseRounds];fire();
    check(witnessHealth==200 && bag[PulseRounds]==ammo && bolts[(nextBolt+bolts.size()-1)%bolts.size()].absorbed,"Rifle immunity/reward failed");
    combatEnabled=true;
    const auto old=g.feet;bool sword=false;
    for(Vec d: {Vec{-1.5,0,.8},Vec{1.5,0,.8},Vec{0,0,-1.5},Vec{0,0,1.5}}){g.feet=witness.feet+d;if(goblinStrike(g,4)){sword=true;break;}}
    g.feet=old;check(sword && witnessHealth==188,"Sword damage failed");
    const auto afterSword=bag[PulseRounds];updateFortressDefenses(.05);
    check(witnessHealth<188 && bag[PulseRounds]==afterSword,"Fortress bolt damage or reward failed");
    const int hp=witnessHealth;saveFile=path.string()+"-temporary.save";check(save(),"Fortress save failed");
    witnessHealth=200;broods[0]={};g2={};load();
    check(witnessHealth==hp && broods[0].slot==5 && std::abs(broods[0].age-23)<.001 && goblin(5).health==0 && inFortress() && surfaceFortress && zoom==FineDepth,"Fortress save roundtrip failed");
    std::filesystem::remove(saveFile);readOnly();
    witnessHealth=8;damageWitness(12);updateWitness(.1);check(witnessHealth==0 && !witness.active,"Witness defeat failed");
    innerFeet={24.5,5.01,30};wheel(-1);
    check(level<0 && !surfaceFortress && goalZoom==FineDepth-1 && length(feet-anchor)<1e-8 && std::abs(std::remainder(yaw-originalYaw,2*Pi))<1e-9,"Grow from anywhere must return to the surface anchor");
    movement(1./60,{});check(level<0,"Growing must not reopen fortress");
    wheel(1);movement(1./60,{});
    check(inFortress() && surfaceFortress && goblin(5).health==0 && broods[0].slot==5 && witnessHealth==0,"Reentry lost fortress state");
    wheel(-20);movement(1./60,{});check(level<0 && goalZoom==0,"Multi-step growth failed");
    std::cout<<"Rifle immunity, sword and defense damage, persistent health and brood, defeat and exit: PASS"<<std::endl;
}
