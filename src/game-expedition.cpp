#include "game.hpp"
#include "deer-art.hpp"
#include "atmosphere.hpp"
#include "natural-world.hpp"
#include "rifle-art.hpp"

namespace {
constexpr double Reach=25;
Vec cranialCant(Vec p){
    constexpr double angle=.095;
    const double c=std::cos(angle),s=std::sin(angle);
    return {c*p.x-s*p.y,s*p.x+c*p.y,p.z};
}
}

void Game::moveInput(double dt,Vec wish){
    if(length(wish)>1)wish=unit(wish);
    const double rate=length(wish)>1e-8?37:88;
    smoothInput+=(wish-smoothInput)*(1-std::exp(-dt*rate));
    if(length(smoothInput)<0.0001)smoothInput={};
    walkCycle=std::fmod(walkCycle+dt*13*length(smoothInput),2*Pi);
    weaponSway=std::lerp(weaponSway,length(smoothInput),1-std::exp(-dt*14));
}

void Game::stance(bool requested){
    if(requested){crouched=true;return;}
    if(!crouched)return;
    crouched=false;
    if(level>=0?layer(family,level).overlaps(innerFeet,innerSize()):overlapsField(feet,body()))
        crouched=true;
}

bool Game::overlapsField(Vec pos,double scale,double height) const{
    if(world.overlaps(pos,scale,height))return true;
    const auto body=World::playerBox(pos,scale,height);
    const auto lo=floorCell(body.lo),hi=floorCell(body.hi);
    for(int z=lo.z;z<=hi.z;z++)for(int y=lo.y;y<=hi.y;y++)for(int x=lo.x;x<=hi.x;x++){
        const Cell cell{x,y,z};
        const auto found=devices.find(cell);
        if(found!=devices.end() && furniture(found->second.id) &&
            body.intersects(expedition::furnitureVolume(found->second.id,cell.v(),!found->second.closed)))return true;
    }
    return false;
}

double Game::sweepField(Vec pos,double scale,int axis,double distance,double height) const{
    auto permitted=world.sweep(pos,scale,axis,distance,height);
    if(std::abs(permitted)<1e-12 || devices.empty())return permitted;
    const auto body=World::playerBox(pos,scale,height);
    auto path=body;
    if(permitted>0)path.hi[axis]+=permitted;
    else path.lo[axis]+=permitted;
    const auto lo=floorCell(path.lo),hi=floorCell(path.hi);
    for(int z=lo.z;z<=hi.z;z++)for(int y=lo.y;y<=hi.y;y++)for(int x=lo.x;x<=hi.x;x++){
        const Cell cell{x,y,z};
        const auto prop=devices.find(cell);
        if(prop==devices.end() || !furniture(prop->second.id))continue;
        const auto volume=expedition::furnitureVolume(prop->second.id,cell.v(),!prop->second.closed);
        if(!path.intersects(volume))continue;
        if(distance>0)permitted=std::min(permitted,std::max(0.0,volume.lo[axis]-body.hi[axis]-1e-7));
        else permitted=std::max(permitted,std::min(0.0,volume.hi[axis]-body.lo[axis]+1e-7));
    }
    return permitted;
}

expedition::Wildlife& Game::wildlife(int set,int depth){
    auto& animals=fauna[set][depth];
    if(!animals)animals=std::make_unique<expedition::Wildlife>();
    animals->prepare(layer(set,depth));
    return *animals;
}

void Game::updateExpedition(double dt){
    hurtCooldown=std::max(0.0,hurtCooldown-dt);
    recoil=std::max(0.0,recoil-dt*4);
    for(auto& bolt:bolts)bolt.age=std::min(1.0,bolt.age+dt);
    if(bag[PowerPack] && camp() && generator.field!=0 && generator.rms2>0.16){
        const double charge=std::min(110.0,std::sqrt(generator.rms2)*17);
        battery=std::min(expedition::BatteryCapacity,battery+charge*dt*timeRate());
    }
    if(armed && aiming && weapon==PulseRifle && bag[PowerPack] && battery>0){
        battery=std::max(0.0,battery-3.0*dt);
        if(battery==0)message("Quartz sight battery is empty");
    }
    if(level<0 || inFortress() || !wantsCapture() || !combatEnabled)return;
    auto& room=layer(family,level);
    auto& area=wildlife(family,level);
    const bool predator=modernWorld && witness.active && witness.family==family && witness.level==level && witness.scale>=.5;
    if(level==expedition::Village)for(int id=0;id<int(area.elves.size());id++){
        auto& elf=area.elves[id];
        if(area.missingElves[id])continue;
        elf.stride+=dt*1.3;
        const bool hunted=predator && witness.horror.mode==dread::Mode::Hunt &&
            witness.horror.prey.kind==dread::Kind::Elf && witness.horror.prey.index==id;
        if(hunted && length(elf.feet-witness.feet)<8){
            const Vec away=unit(Vec{elf.feet.x-witness.feet.x,0,elf.feet.z-witness.feet.z});
            const Vec home{20.5+(id%2)*8,elf.feet.y,13.5+(id/2)*10};
            for(double turn:{0.,.9,-.9,1.6,-1.6}){
                const Vec direction=away*std::cos(turn)+Vec{away.z,0,-away.x}*std::sin(turn);
                Vec next=elf.feet;
                for(int axis:{0,2})next[axis]+=room.sweep(next,1.02,axis,direction[axis]*dt*1.7,1.8);
                if(length(next-elf.feet)<dt*.3 || length(next-home)>4.5)continue;
                const double drop=room.sweep(next,1.02,1,-.22,1.8);
                if(drop<=-.219)continue;
                elf.feet=next+Vec{0,drop,0};elf.yaw=std::atan2(direction.x,direction.z);elf.stride+=dt*7;break;
            }
        }else{
            const auto to=innerFeet-elf.feet;
            if(length(to)<5)elf.yaw+=std::remainder(std::atan2(to.x,to.z)-elf.yaw,2*Pi)*(1-std::exp(-dt*5));
        }
    }
    const int attacks=area.advance(room,innerFeet,innerSize(),dt*std::max(0.45,std::pow(0.72,innerZoom)),
        predator?std::optional<Vec>{witness.feet}:std::nullopt);
    if(attacks && hurtCooldown==0){
        health=std::max(0,health-20);
        hurtCooldown=0.65;
        if(health>0){message("Delbert took 20 damage");return;}
        health=100;
        feet={0.5,29,-7.1};zoom=goalZoom=0;precision=0;
        family=level=-1;
        hurtCooldown=0;
        armed=sightOn=flying=false;
        vertical.reset();smoothInput={};
        yaw=Pi;pitch=-0.16;
        world.stream(floorCell(feet),2);
        message("Delbert recovered at camp");
    }
}

void Game::fire(){
    const Vec eye=camera(),ray=viewDirection(yaw,pitch);
    const double size=level>=0?innerSize():body();
    const double reach=Reach*std::max(size,0.005)*(weapon==PulseRifle?1.5:1.0);
    double obstacle=reach;
    const auto terrain=witnessRay(eye,ray,reach);
    if(terrain)obstacle=terrain->distance;
    if(bag[PulseRounds]<FullBlock){
        message("No pulse rounds. Craft five at a Prism Press.");
        return;
    }
    bag[PulseRounds]-=FullBlock;
    recoil=1;
    const double intercept=witnessHit(eye,ray,obstacle);
    const bool absorbed=intercept<obstacle;
    if(absorbed)obstacle=intercept;
    if(level<0){
        for(auto& [id,deer]:forestDeer){
            const double at=rayBox(eye,ray,deer.feet-Vec{.22,-.42,.42},deer.feet+Vec{.22,1.3,.42},obstacle);
            if(at<obstacle){obstacle=at;deer.idle=-1;}
        }
    }
    double guardStop;const int guardID=goblinTarget(eye,ray,obstacle,guardStop);
    double stop=guardStop;
    const int damage=weapon==PulseRifle?85:60;
    if(level>=0 && !inFortress() && wildlife(family,level).hit(eye,ray,guardStop,stop,damage))message("Crawler down");
    else if(guardID>=0 && guardStop<obstacle){
        auto& guard=guards[guardID];
        guard.health=std::max(0,guard.health-damage);
        if(guard.health==0)guard.beginCollapse(dot(ray,{std::cos(guard.yaw),0,-std::sin(guard.yaw)}));
        guard.alert=true;
        message(guard.health>0?"The goblin is wounded":"The goblin is down");
    }
    const bool reachedWitness=absorbed && stop==intercept && guardStop==intercept;
    if(reachedWitness){
        const int item=dread::reward(bag,lootState);
        message(item==Air?"Round returned. Material storage full.":std::string("Round returned  |  +1 ")+materialName(item));
    }
    auto& bolt=bolts[nextBolt++%bolts.size()];
    bolt={eye,eye+ray*stop,0,reachedWitness,size,family,level};
}

void Game::makeExpeditionModels(){
    makeHorrorModels();
    Geometry enemy,proxy,weapon,optic,round,flash,longGun;
    expedition::humanoid(enemy,false,0);hostileModel.upload(enemy);
    proxy.box({-0.23,0.41,-0.17},{0.23,1.19,0.16},ClothTexture,{0.43f,0.32f,0.56f});
    proxy.stone({0,1.43,0.03},0.17,SkinTexture,{0.73f,0.80f,0.53f},3,6);
    for(double sign:{-1.,1.}){
        proxy.box({sign*0.12-0.072,0.02,-0.09},{sign*0.12+0.072,0.54,0.13},LeatherTexture,{0.75f,0.65f,0.78f});
        proxy.tube({sign*0.27,1.08,0},{sign*0.36,0.88,0.50},0.075,ClothTexture,{0.43f,0.32f,0.56f},4);
        proxy.stone({sign*0.05,1.45,0.17},0.024,Lamp,{1.18f,0.59f,0.32f},3,4);
    }
    hostileProxy.upload(proxy);
    for(int i=0;i<int(elfModels.size());i++){
        Geometry elf;expedition::humanoid(elf,true,i);elfModels[i].upload(elf);
    }
    expedition::pulseSidearm(weapon,optic,round,flash);
    // The integrated optic is a permanent part of the rifle view model.
    Geometry rifleOptic,accent,lens;
    rifleart::build(longGun,rifleOptic,accent,lens);
    sightModel.upload(rifleOptic);rifleGlow.upload(accent);rifleLens.upload(lens);
    rifle.upload(longGun);
    witnessHead(witnessFace.geometry,&witnessJaw.geometry);
    for(auto* part:{&witnessFace,&witnessJaw})for(auto& v:part->geometry.points){
        const auto p=cranialCant({v.x,v.y,v.z}),n=cranialCant({v.nx,v.ny,v.nz});
        v.x=p.x;v.y=p.y;v.nx=n.x;v.ny=n.y;
    }
    cleanWitnessFace=witnessFace.geometry;cleanWitnessJaw=witnessJaw.geometry;
    witnessFace.upload();witnessJaw.upload();
    for(int i=0;i<2;i++){
        witnessEye(witnessEyes[i].geometry,i==0);witnessEyes[i].upload();
        witnessSkins[i].build(i!=0);
        northRigs[i].build(i!=0);northRigs[i].pose(northPosed,0,0,0,true,true);northModels[i].uploadIndexed(northPosed,northRigs[i].indices);
    }
    sidearm.upload(weapon);roundModel.upload(round);flashModel.upload(flash);
}

void Game::makeParentScenery(){
    Geometry exterior;
    if(inFortress()){parentScenery.clear();sceneryFamily=family;sceneryLevel=level;return;}
    const auto entry=micro::Arrival;
    const bool surface=level==0;
    const Vec anchor=surface?gateAnchor(family):micro::anchor(family,level);
    const double ratio=1/micro::parentScale(level);
    const auto middle=floorCell(anchor);
    const int extent=surface?12:9;
    const auto sample=[&](Cell p){return surface?world.sample(0,p).id:layer(family,level-1).at(p);};
    for(int z=middle.z-extent;z<=middle.z+extent;z++)for(int x=middle.x-extent;x<=middle.x+extent;x++)
        for(int y=middle.y-7;y<=middle.y+12;y++){
            const Cell p{x,y,z};
            const int id=sample(p);
            if(id<=Air || id>Bedrock || id==Water || id==Bedrock)continue;
            for(int side=0;side<6;side++)if(!sample(p+Sides[side])){
                micro::backdropFace(exterior,entry+(p.v()-anchor)*ratio,ratio,side,id,{0.86f,0.86f,0.83f});
            }
        }
    parentScenery.upload(exterior);
    sceneryFamily=family;sceneryLevel=level;
}

void Game::drawExpedition(const View& view,double aspect){
    if(inFortress())return;
    auto& animals=wildlife(family,level);
    const auto eye=camera();
    for(int i=0;i<expedition::Hostiles;i++){
        const auto& mob=animals.enemies[i];
        const Box bounds{mob.feet-Vec{0.48,0.10,0.48},mob.feet+Vec{0.48,1.60,0.48}};
        if(animals.defeated[i] || !view.sees(bounds,aspect,39))continue;
        const auto lift=std::sin(mob.stride)*0.045;
        const bool far=length(mob.feet-eye)>13;
        renderer.draw(far?hostileProxy:hostileModel,mob.feet+Vec{0,lift,0},0.85,mob.yaw,nullptr,0.15f,0,far?0.60f:0,false,0,
            std::sin(mob.stride)*0.22);
    }
    if(level!=expedition::Village)return;
    if(villageFamily!=family){
        Geometry path;
        expedition::villageStreet(path);
        villagePath.upload(path);
        for(int tree=0;tree<int(villageGrove.size());tree++){
            Geometry full,low;
            expedition::villageTree(full,tree,family,true);
            expedition::villageTree(low,tree,family,false);
            villageGrove[tree].full.upload(full);villageGrove[tree].proxy.upload(low);
        }
        villageFamily=family;
    }
    renderer.draw(villagePath);
    for(int tree=0;tree<int(villageGrove.size());tree++){
        const auto center=expedition::villageTreeCenter(tree);
        if(!view.sees({center-Vec{8.8,0.2,8.8},center+Vec{8.8,18.3,8.8}},aspect,57))continue;
        const bool near=std::hypot(eye.x-center.x,eye.z-center.z)<11;
        renderer.draw(near?villageGrove[tree].full:villageGrove[tree].proxy,{},1,0,nullptr,0,0,near?0:0.55f);
    }
    for(std::size_t i=0;i<animals.elves.size();i++){
        if(animals.missingElves[i])continue;
        auto& elf=animals.elves[i];
        if(!view.sees({elf.feet-Vec{0.55,0.10,0.55},elf.feet+Vec{0.55,1.95,0.55}},aspect,40))continue;
        const double idle=std::sin(elf.stride+double(i))*0.009;
        renderer.draw(elfModels[i],elf.feet+Vec{0,idle,0},1.02,elf.yaw,nullptr,0.33f);
    }
}

void Game::drawWeapon(){
    const auto eye=camera(),dir=viewDirection(yaw,pitch),right=unit(cross(dir,{0,1,0})),up=cross(right,dir);
    for(const auto& bolt:bolts){
        if(bolt.age>=0.28 || bolt.family!=family || bolt.level!=level)continue;
        const double phase=std::min(1.0,bolt.age/0.065);
        const auto at=bolt.from+(bolt.to-bolt.from)*phase;
        const auto axis=bolt.to-bolt.from;
        const auto angle=std::atan2(axis.x,axis.z);
        const double shotPitch=std::atan2(axis.y,std::hypot(axis.x,axis.z));
        if(phase<1)renderer.draw(roundModel,at,0.35*bolt.scale,angle,nullptr,1.45f,0,0,false,shotPitch);
        else if(bolt.absorbed && bolt.age<.19){
            const double remaining=1-(bolt.age-.065)/.125;
            renderer.draw(witnessImpact,bolt.to,bolt.scale*(.55+.55*remaining),angle,nullptr,float(2.6*remaining));
        }else if(!bolt.absorbed)renderer.draw(flashModel,bolt.to,bolt.scale*0.37*(1-bolt.age/0.28),angle,nullptr,1.45f);
    }
    if(!armed)return;
    const double size=level>=0?innerSize():body();
    const double shake=0.018*weaponSway;
    const bool rifleEquipped=weapon==PulseRifle;
    const Vec offset=dir*((rifleEquipped?rifleart::Forward:0.66)-recoil*0.085)
        +right*((aiming?(rifleEquipped?0.0:0.012):(rifleEquipped?rifleart::Side:0.31))+std::sin(walkCycle)*shake)
        -up*((aiming?(rifleEquipped?rifleart::AimDrop:0.10):(rifleEquipped?rifleart::Drop:0.27))+std::cos(walkCycle)*shake-recoil*0.022);
    const double rotation=Pi-yaw+(rifleEquipped && !aiming?rifleart::HipTurn:0.0);
    // Depth-test the rifle so the many layered surfaces actually occlude
    // one another, instead of drawing later panels over nearer metal.
    renderer.overlay(!rifleEquipped);
    const Vec at=eye+offset*size;
    const double tilt=pitch+recoil*0.08;
    const double modelScale=(rifleEquipped?rifleart::Scale:.56)*size;
    renderer.draw(rifleEquipped?rifle:sidearm,at,modelScale,rotation,nullptr,0.68f,0,0,false,tilt);
    if(rifleEquipped){
        renderer.draw(sightModel,at,modelScale,rotation,nullptr,0.68f,0,0,false,tilt);
        renderer.translucent(true);
        renderer.draw(rifleLens,at,modelScale,rotation,nullptr,0,0,0,false,tilt);
        renderer.translucent(false);
        renderer.draw(rifleGlow,at,modelScale,rotation,nullptr,
            bag[PowerPack] && battery>0?1.0f:0.4f,0,0,false,tilt);
    }
    if(recoil>0.77){
        const Vec muzzle=rifleEquipped?Vec{std::sin(rotation)*std::cos(tilt),std::sin(tilt),std::cos(rotation)*std::cos(tilt)}*(1.18*modelScale):dir*(.43*size);
        renderer.draw(flashModel,at+muzzle,0.62*size,rotation,nullptr,1.7f);
    }
    renderer.overlay(false);
}


int Game::fixtureAt(Cell cell) const{
    const auto found=devices.find(cell);
    if(found==devices.end() || !furniture(found->second.id))return Air;
    return found->second.closed?found->second.id:alternateFurniture(found->second.id);
}

std::optional<Hit> Game::witnessRay(Vec from,Vec dir,double reach){
    if(level>=0)return layer(family,level).ray(from,dir,reach,realmTier());
    auto terrain=world.ray(from,dir,precision,reach);
    if(!devices.empty()){
        auto prop=expedition::furnitureRay(from,dir,terrain?terrain->distance:reach,
            [this](Cell cell){return fixtureAt(cell);});
        if(prop)return prop;
    }
    return terrain;
}

void Game::updateWitness(double dt){
    if(witnessHealth<=0){witness.active=false;return;}
    if(modernWorld && !wantsCapture() && witness.horror.mode!=dread::Mode::Watch)return;
    moveWitness(dt);
    if(witness.active)updateWitnessRig(dt);
}

void Game::moveWitness(double dt){
    const bool surface=level<0;
    const Vec player=surface?feet:innerFeet;
    const Vec eye=camera(),forward=viewDirection(yaw,pitch);
    const auto right=unit(cross(forward,{0,1,0}));
    const double scale=surface?body():innerSize();
    const double aspect=double(fieldWidth)/fieldHeight;
    const auto obstructed=[&](Vec from,Vec to){
        const auto offset=to-from;
        const double distance=length(offset);
        if(distance<scale*0.05)return false;
        const auto ray=offset/distance;
        const double end=distance-scale*0.025;
        return witnessRay(from,ray,end).has_value();
    };
    const auto seen=[&](Vec position,bool low){
        const auto torso=position+Vec{0,scale*(low?1.0:3.45),0};
        const auto head=position+Vec{0,scale*(low?1.77:4.56),0};
        for(const auto part:{torso,head}){
            const auto delta=part-eye;
            const double depth=dot(forward,delta);
            if(depth>0 && std::abs(dot(right,delta))<depth*aspect+scale*.6 &&
                std::abs(dot(cross(right,forward),delta))<depth+scale*.6 && !obstructed(eye,part))return true;
        }
        return false;
    };
    if(witness.family!=family || witness.level!=level){
        witness={};
        witness.family=family;witness.level=level;
    }
    if(witness.scale!=scale){
        const bool knew=witness.sight.known && witness.sight.age<.35;
        witness.sight={};witness.horror.finish(12);witness.horror.interestKnown=false;
        if(knew){witness.horror.notice(player);witness.horror.cooldown=3;}
    }
    witness.scale=scale;
    const auto fits=[&](Vec point,double height){
        if(surface)return point.y>0 && !overlapsField(point,scale*4.15,height/4.15);
        const Vec upper=point+Vec{0,height*scale,0};
        const Vec sides{scale,0,scale};
        return micro::within(floorCell(point-sides)) && micro::within(floorCell(upper+sides)) &&
            !layer(family,level).overlaps(point,scale*4.15,height/4.15);
    };
    if(witness.active && witness.climbing && fits(witness.feet+witness.climbDirection*(scale*0.21),2.25)){
        const Vec lip=witness.feet+witness.climbDirection*(scale*1.16)+Vec{0,scale*0.32,0};
        if(!witnessRay(lip,{0,-1,0},scale*0.70)){
            witness.climbing=false;witness.falling=true;witness.gripWait=0;
        }
    }
    if(witness.active && !witness.climbing && (modernWorld || witness.falling || witness.groundedPose) &&
        fits(witness.feet,2.25)){
        constexpr double width=4.15;
        const bool low=witness.crouched || !fits(witness.feet,5);
        const double height=(low?2.25:5.0)/width;
        const auto floorSweep=[&](double distance){
            return surface?sweepField(witness.feet,scale*width,1,distance,height):
                layer(family,level).sweep(witness.feet,scale*width,1,distance,height);
        };
        const double nextSpeed=std::min(22.0,witness.fallSpeed+dt*19.0);
        const double step=-(nextSpeed+witness.fallSpeed)*0.5*dt*scale;
        const double drop=floorSweep(step);
        witness.feet.y+=drop;
        witness.falling=drop<=step+std::max(2e-9,scale*0.0005);
        witness.fallSpeed=witness.falling?nextSpeed:0;
    }
    const double range=length(witness.feet-player)/scale;
    witnessCueCooldown=std::max(0.0,witnessCueCooldown-dt);
    witness.step=0;
    witness.dreadPhase+=dt*(1+1.65*witness.rage);
    witness.rage=std::max(0.,witness.rage-dt*.008);
    // Deterministic posture timers run in real seconds, independent of frame rate.
    witness.uprightCooldown=std::max(0.0,witness.uprightCooldown-dt);
    witness.uprightTime=std::max(0.0,witness.uprightTime-dt);
    const auto grounded=[&](Vec candidate){
        const Vec probe=candidate+Vec{0,8*scale,0};
        if(fits(probe,2.25)){
            const auto fall=surface?sweepField(probe,scale*4.15,1,-15*scale,2.25/4.15):
                layer(family,level).sweep(probe,scale*4.15,1,-15*scale,2.25/4.15);
            if(fall>-14.998*scale)candidate=probe+Vec{0,fall,0};
        }
        return candidate;
    };

    // If edits or a scale change enclose the hull, walking and climbing may
    // both be impossible. Use the usual unseen spawn search as a last resort.
    const bool watched=witness.active && range<=20 && (seen(witness.feet,true) ||
        (witness.uprightTime>0 && seen(witness.feet,false)));
    const bool wedged=witness.horror.mode==dread::Mode::Watch && witness.blockedTime>=2.4 && !watched && !seen(witness.feet,true);
    witness.wait=std::max(0.0,witness.wait-dt);
    if((!witness.active || (range<3 && !watched && !modernWorld) || wedged) && witness.wait==0){
        for(int attempt=0;attempt<12;attempt++){
            const double theta=yaw+0.65+attempt*2.399963;
            const auto candidate=grounded(player+Vec{std::sin(theta),0,std::cos(theta)}*(scale*(12+attempt%4)));
            const bool low=!fits(candidate,5);
            if((low && !fits(candidate,2.25)) || seen(candidate,low))continue;
            if(wedged && fits(candidate-Vec{0,scale*0.1,0},2.25))continue;
            witness.feet=candidate;
            witness.heading=std::atan2(player.x-candidate.x,player.z-candidate.z);
            witness.active=true;witness.stareTime=0;witness.observed=false;
            witness.climbing=false;witness.falling=false;witness.fallSpeed=0;witness.crestDistance=0;
            witness.crouched=true;
            // Stalk for a while before the first possible full-height reveal.
            witness.uprightTime=0;
            witness.uprightCooldown=9.0+double(CellHash{}(floorCell(candidate))%9);
            witness.rig.ready=false;witness.gazeReady=false;witness.sight={};
            witness.coverTime=0;
            witness.pace=0;witness.surge=0;witness.gripWait=0;
            witness.detourLeft=witness.climbRetry=witness.blockedTime=witness.progressTime=0;
            witness.travelDirection={};witness.seekingCover=witness.holdingCover=false;witness.coverLean={};witness.coverRetry=0;
            return;
        }
        witness.wait=0.35;
    }
    if(!witness.active)return;
    // Keep watching the low silhouette throughout a rise so the camera
    // looking at its feet does not make the creature flicker between poses.
    const bool canRise=!witness.climbing && !witness.falling && fits(witness.feet,5.0);
    if(!canRise)witness.uprightTime=0;
    const Vec head=witness.rig.ready?witness.rig.frame.point(witness.rig.pose.neck):
        witness.feet+Vec{0,scale*(witness.crouched?1.77:4.56),0};
    const bool hasLine=range<=32 && !obstructed(head,eye);
    witness.sight.observe(hasLine?std::optional<Vec>{eye}:std::nullopt,dt,scale);
    // Extrapolate a short distance from the last clear sighting. Once that
    // lead expires, investigate the last position and circle it to reacquire.
    const bool tracking=modernWorld && witness.sight.known;
    const Vec remembered=witness.sight.predicted();
    const Vec quarry=tracking?remembered:player;
    const bool special=modernWorld && combatEnabled && wantsCapture() && scale>=.25 &&
        horrorPlan(dt,watched,hasLine,eye,head,fits,grounded,obstructed);
    if(watched){
        witness.observed=true;
        const double prior=witness.stareTime;
        witness.stareTime=std::min(1.0,prior+dt);
        // Normally crawl on all fours. Occasionally unfold in direct sight
        // for a short threat display, but never in low clearance or mid-climb.
        if(!special && canRise && !witness.seekingCover && !witness.holdingCover && witness.stareTime>=0.85 && witness.uprightCooldown<=0 &&
            witness.uprightTime<=0){
            const auto key=CellHash{}(floorCell(witness.feet));
            witness.uprightTime=2.0+0.15*double((key>>4)%6);
            witness.uprightCooldown=28.0+double((key>>9)%15);
        }
        witness.crouched=witness.uprightTime<=0;
        if(!special && prior<0.40 && witness.stareTime>=0.40 && witnessCueCooldown==0 && wantsCapture() && combatEnabled){
            const auto offset=unit(witness.feet-eye);
            witnessSound(std::clamp(dot(offset,right),-1.0,1.0),std::clamp(1.15-range*0.031,0.5,0.93));
            witnessCueCooldown=18.0;
        }
    }else{
        witness.uprightTime=0;
        witness.crouched=true;
        if(witness.observed && witness.stareTime>=0.40)witness.surge=0.42;
        witness.observed=false;witness.stareTime=0;
    }
    const auto toPlayer=unit(Vec{quarry.x-witness.feet.x,0,quarry.z-witness.feet.z});
    const Vec tangent{toPlayer.z,0,-toPlayer.x};
    // Long stalking passes followed by an abrupt, short approach, then a retreat.
    // The existing world collision sweeps and contact solver still own every step.
    const double rhythm=.5+.5*std::sin(witness.dreadPhase*.73+std::sin(witness.dreadPhase*.19)*1.7);
    const double pressure=modernWorld && !watched?std::pow(rhythm,7.0)*.65:0.0;
    Vec goal=unit(toPlayer*(range>20?1.0:range<10?-0.85:.08+pressure)+tangent*(range>20?.08:.82));
    if(modernWorld && !special){
        witness.coverRetry=std::max(0.0,witness.coverRetry-dt);
        // Cover is a committed destination, not an action cancelled merely
        // because the player notices the creature moving towards it.
        if(range>20 || range<4 || witness.climbing || witness.falling){
            witness.seekingCover=witness.holdingCover=false;witness.coverLean={};
        }
        const Vec observer=tracking?remembered:eye;
        const auto peek=[&](Vec p){return stalking::peekAt(p,observer,scale,obstructed);};
        witness.coverTime=witness.holdingCover?witness.coverTime+dt:0;
        const bool lostTrail=tracking && !hasLine && witness.sight.age>1.1;
        const bool relocate=witness.holdingCover && lostTrail && witness.coverTime>2.0;
        const auto horizontal=[](Vec v){v.y=0;return length(v);};
        if(witness.seekingCover && (horizontal(witness.coverGoal-witness.feet)<.32*scale || witness.blockedTime>1.2)){
            witness.seekingCover=false;witness.coverRetry=0;
        }
        if(range<=20 && range>=4 && !witness.climbing && !witness.falling && !witness.seekingCover && witness.coverRetry<=0){
            witness.coverRetry=2.6;
            witness.holdingCover=false;witness.coverLean={};
            if(auto here=peek(witness.feet);here && !relocate){
                witness.holdingCover=true;witness.coverLean=here->offset;
            }else{
                double best=-1e9;
                // Survey actual trunk sites before generic terrain cover. A
                // ray-based test rejects missing/mined trees and bare hills.
                std::vector<Vec> candidates;
                if(surface && world.seed && world.sculptedTerrain && scale>.5){
                    const Cell tile=parent(floorCell(witness.feet),32);
                    for(const auto& tree:natural::treesNearTile(tile.x,tile.z,*world.seed)){
                        const Vec trunk{tree.x+.5,witness.feet.y,tree.z+.5};
                        if(horizontal(trunk-witness.feet)>12*scale)continue;
                        const Vec away=unit(Vec{trunk.x-observer.x,0,trunk.z-observer.z});
                        const Vec lateral{away.z,0,-away.x};
                        const double radius=tree.elder?3.3:2.6;
                        for(double sign:{-1.,1.})for(double f:{.48,.78,1.02})
                            candidates.push_back(trunk+away*(radius+1.1*scale)+lateral*(sign*(radius*f+.25*scale)));
                    }
                }
                for(double radius:{2.8,5.0,8.0})for(int i=0;i<12;++i){
                    const double angle=i*2*Pi/12;
                    candidates.push_back(witness.feet+(toPlayer*std::cos(angle)+tangent*std::sin(angle))*(radius*scale));
                }
                // Nearest sites first, with a hard query budget on the game
                // thread. Most open positions are rejected before a sweep.
                std::stable_sort(candidates.begin(),candidates.end(),[&](Vec a,Vec b){
                    return horizontal(a-witness.feet)<horizontal(b-witness.feet);
                });
                int examined=0;
                for(Vec candidate:candidates){
                    const double travel=horizontal(candidate-witness.feet)/scale;
                    if(travel<.4 || travel>13 || horizontal(candidate-observer)>20*scale || horizontal(candidate-observer)<5*scale)continue;
                    if(++examined>64)break;
                    if(surface && world.seed && world.sculptedTerrain){
                        const auto* roof=world.roof(floorCell(candidate));
                        if(!roof)continue; // don't force synchronous chunk generation for AI
                        candidate.y=roof->height(candidate.x-std::floor(candidate.x),candidate.z-std::floor(candidate.z));
                    }
                    if(!obstructed(observer,candidate+Vec{0,1.05*scale,0}))continue;
                    // Ground each candidate with the actual collision hull.
                    const Vec probe=candidate+Vec{0,2.8*scale,0};
                    if(!fits(probe,2.25))continue;
                    const double fall=surface?sweepField(probe,scale*4.15,1,-5.6*scale,2.25/4.15):
                        layer(family,level).sweep(probe,scale*4.15,1,-5.6*scale,2.25/4.15);
                    if(fall<-5.59*scale)continue;
                    candidate=probe+Vec{0,fall,0};
                    if(!fits(candidate,2.25))continue;
                    if(auto plan=peek(candidate)){
                        const Vec lead=unit(witness.sight.velocity);
                        const Vec shift=unit(candidate-witness.feet);
                        const double score=plan->quality*3.0-travel*.28+dot(lead,shift)*.85;
                        if(score>best){best=score;witness.coverGoal=candidate;witness.seekingCover=true;}
                    }
                }
                if(witness.seekingCover){witness.detourLeft=0;witness.travelDirection={};witness.blockedTime=0;}
            }
        }
        if(witness.seekingCover || witness.holdingCover){witness.crouched=true;witness.uprightTime=0;}
        if(range<=20 && !witness.climbing && !witness.seekingCover &&
            (watched || (witness.holdingCover && !relocate))){
            witness.pace=0;witness.travelDirection={};witness.detourLeft=0;
            witness.progressTime=witness.blockedTime=0;
            // Turn towards the player at a measured pace without walking.
            const double shoulderLead=witness.holdingCover?0.0:.36;
            const double bearing=std::atan2(toPlayer.x,toPlayer.z)+shoulderLead;
            witness.heading+=std::clamp(std::remainder(bearing-witness.heading,2*Pi),-dt*.65,dt*.65);
            return;
        }
        if(lostTrail){
            const double gap=horizontal(remembered-witness.feet)/scale;
            goal=gap>2.0?unit(toPlayer+tangent*.22):unit(tangent+toPlayer*.10);
        }
        if(witness.seekingCover)goal=unit(Vec{witness.coverGoal.x-witness.feet.x,0,witness.coverGoal.z-witness.feet.z});
    }
    if(special){
        auto& h=witness.horror;
        witness.seekingCover=witness.holdingCover=false;witness.coverLean={};
        witness.uprightTime=0;witness.crouched=true;
        const Vec delta{h.goal.x-witness.feet.x,0,h.goal.z-witness.feet.z};
        const bool stop=h.mode==dread::Mode::Feed || h.mode==dread::Mode::Concealed ||
            (h.mode==dread::Mode::Hunt && h.prey.kind==dread::Kind::Goblin && h.engageAge>0) ||
            (h.mode==dread::Mode::Inspect && (length(delta)<2.8*scale || range<4));
        if(stop){
            witness.pace=0;witness.travelDirection={};witness.detourLeft=0;witness.blockedTime=witness.progressTime=0;
            if(h.mode!=dread::Mode::Concealed && length(delta)>.01){
                const double bearing=std::atan2(delta.x,delta.z);
                witness.heading+=std::clamp(std::remainder(bearing-witness.heading,2*Pi),-dt*1.3,dt*1.3);
            }
            return;
        }
        goal=unit(delta);
    }
    const Vec direction=length(witness.travelDirection)>0.5 &&
        dot(witness.travelDirection,goal)>-0.9?
        unit(witness.travelDirection+(goal-witness.travelDirection)*(-std::expm1(-7*dt))):goal;
    witness.surge=std::max(0.0,witness.surge-dt);
    const double speedGoal=special?(witness.horror.mode==dread::Mode::Hunt?4.4+1.8*witness.rage:2.7):modernWorld && witness.seekingCover?2.35:watched?(witness.crouched?1.65:1.05):
        (range>20?4.1+std::min(12.0,(range-20)*0.20):2.3)+witness.surge*6;
    const double tension=!special && modernWorld && range>10 && range<22?
        (watched?.52+.48*std::pow(rhythm,2):.72+1.05*std::pow(rhythm,5)):1.0;
    witness.pace+=(speedGoal*tension-witness.pace)*(-std::expm1(-12*dt));
    const double stride=scale*dt*witness.pace;
    const auto sweep=[&](Vec point,int axis,double amount){
        constexpr double width=4.15,lowHull=2.25/width;
        return surface?sweepField(point,scale*width,axis,amount,lowHull):
            layer(family,level).sweep(point,scale*width,axis,amount,lowHull);
    };
    const auto slide=[&](Vec point,Vec move){
        for(int axis:{0,2})point[axis]+=sweep(point,axis,move[axis]);
        return point;
    };
    witness.climbRetry=std::max(0.0,witness.climbRetry-dt);
    if(witness.progressTime==0)witness.progressOrigin=witness.feet;
    witness.progressTime+=dt;
    if(witness.progressTime>=0.60){
        if(length(witness.feet-witness.progressOrigin)<scale*0.20){
            witness.blockedTime+=witness.progressTime;
            witness.climbing=false;
            witness.climbRetry=1.25;
            witness.detourLeft=0;
        }else witness.blockedTime=0;
        witness.progressTime=0;
    }
    const auto climb=[&]{
        auto& place=witness.feet;
        const Vec direction=witness.climbDirection;
        const double speed=scale*dt*3.7;
        const bool onWall=!fits(place+direction*scale*0.21,2.25);
        // Let a hand or foot take the load before pulling the trunk up the wall.
        if(onWall && witness.crestDistance==0){
            const bool anchored=std::any_of(witness.rig.limbs.begin(),witness.rig.limbs.end(),
                [&](const contact::Limb& limb){
                    return limb.phase==contact::Phase::Stance &&
                        (dot(limb.support.normal,direction)<-0.32 ||
                        (limb.support.normal.y>0.45 && limb.support.point.y>place.y+scale*0.28));
                });
            if(!anchored){
                witness.gripWait+=dt;
                if(witness.gripWait>0.22)return false;
                // Keep travelling along the face while the hands look for purchase.
                // If neither side is open, let the ground planner act this frame.
                const Vec along{-direction.z,0,direction.x};
                for(double sign:{1.0,-1.0}){
                    const Vec next=slide(place,(direction*0.50+along*(0.55*sign))*speed);
                    if(length(next-place)>0.2*speed && fits(next,2.25)){
                        place=next;witness.step=1;
                        return true;
                    }
                }
                return false;
            }
            witness.gripWait=0;
        }
        Vec next=place;
        bool landed=false;
        if(onWall && witness.crestDistance==0){
            next.y+=sweep(place,1,speed);
            if(next.y-place.y<speed*0.08)return false;
        }else{
            next=slide(place,direction*speed);
            witness.crestDistance+=length(next-place)/scale;
            const Vec footing=next+Vec{0,scale*0.08,0};
            const bool supported=witness.crestDistance>0.35 && witnessRay(footing,{0,-1,0},scale*0.31).has_value();
            landed=supported || witness.crestDistance>2.35;
            if(length(next-place)<speed*0.08)witness.crestDistance=0;
        }
        if(length(next-place)<scale*1e-8 || !fits(next,2.25))return false;
        place=next;
        witness.fallSpeed=0;witness.falling=false;
        if(landed){
            witness.climbing=false;
            witness.crouched=true;
            witness.uprightTime=0;
        }
        const double heading=std::atan2(direction.x,direction.z);
        witness.heading+=std::remainder(heading-witness.heading,2*Pi)*std::min(1.0,dt*10);
        witness.step=1;
        return true;
    };
    const bool lowPassage=!canRise && !fits(witness.feet+Vec{0,scale*1.05,0},2.25);
    const bool steerEarly=!witness.climbing && lowPassage && witness.detourLeft<=0 &&
        !fits(witness.feet+direction*scale*0.68,2.25);
    if(witness.climbing){
        if(climb())return;
        witness.climbing=false;witness.climbRetry=1.25;
    }else if(!witness.seekingCover && (!special || witness.horror.mode==dread::Mode::Hunt || witness.horror.mode==dread::Mode::Inspect) && !lowPassage && witness.detourLeft<=0 && witness.climbRetry<=0 && length(direction)>0 && !fits(witness.feet+direction*std::max(stride,scale*0.10),2.25)){
        witness.climbDirection=direction;
        witness.climbing=true;witness.crestDistance=0;witness.gripWait=0;
        witness.crouched=true;
        if(climb())return;
        witness.climbing=false;witness.climbRetry=1.25;
    }
    const auto supported=[&](Vec candidate)->std::optional<Vec>{
        const Vec probe=candidate+Vec{0,scale*0.34,0};
        if(!fits(probe,2.25))return candidate;
        const auto fall=surface?sweepField(probe,scale*4.15,1,-scale*.85,2.25/4.15):
            layer(family,level).sweep(probe,scale*4.15,1,-scale*.85,2.25/4.15);
        if(fall>-scale*.849){
            const Vec floor=probe+Vec{0,fall,0};
            if(floor.y<candidate.y-scale*0.025 && (modernWorld || witness.groundedPose))
                return candidate; // gravity takes the drop over subsequent frames
            return floor;
        }
        if(!modernWorld && !fits(witness.feet-Vec{0,scale*.10,0},2.25))return {};
        return candidate;
    };
    const auto walk=[&](Vec move){
        const Vec start=witness.feet;
        const auto offset=move*stride;
        Vec next=slide(start,offset);
        if(dot(next-start,move)<stride*0.65){
            // Sweep the raised hull across the tread before settling onto it.
            const double up=sweep(start,1,scale*0.34);
            const Vec raised=start+Vec{0,up,0};
            const Vec step=slide(raised,offset);
            if(up>0 && dot(step-raised,move)>dot(next-start,move)){
                const double down=sweep(step,1,-up-scale*0.02);
                if(down>-up-scale*0.019)next=step+Vec{0,down,0};
            }
        }
        if(dot(next-start,move)<stride*0.55)return false;
        const auto landing=supported(next);
        if(!landing || !fits(*landing,2.25) || std::abs(landing->y-start.y)>scale*.36)return false;
        witness.feet=*landing;
        const Vec travel=*landing-start;
        witness.travelDirection=unit(Vec{travel.x,0,travel.z});
        if(witness.uprightTime>0 && !fits(*landing,5)){
            witness.crouched=true;witness.uprightTime=0;
        }
        const double heading=std::atan2(travel.x,travel.z);
        witness.heading+=std::remainder(heading-witness.heading,2*Pi)*std::min(1.0,dt*4.8);
        witness.step=1;
        return true;
    };
    if(witness.detourLeft>0){
        if(walk(witness.detour)){
            witness.detourLeft-=stride/scale;
            return;
        }
        witness.detourLeft=0;
    }
    if(!steerEarly && walk(direction))return;
    // Probe both sides and behind with the full body. Commit to a clear
    // heading long enough to pass the corner before trying the approach again.
    const Vec side{-direction.z,0,direction.x};
    Vec best{};
    double bestScore=0;
    for(const double angle:{0.70,-0.70,1.35,-1.35,1.9,-1.9,2.5,-2.5,Pi}){
        const Vec heading=direction*std::cos(angle)+side*std::sin(angle);
        Vec probe=witness.feet;
        double clearance=0;
        for(int i=0;i<4;i++){
            const Vec next=slide(probe,heading*(scale*0.50));
            if(dot(next-probe,heading)<scale*0.43 || !fits(next,2.25))break;
            clearance+=0.5;probe=next;
        }
        const double score=clearance*(1+0.22*dot(heading,direction)+0.12*dot(heading,witness.detour));
        if(score>bestScore){best=heading;bestScore=score;}
    }
    if(bestScore>0 && walk(best)){
        witness.detour=best;
        witness.detourLeft=1.8;
        witness.climbRetry=0.7;
    }else if(steerEarly)walk(direction);
}

void Game::updateWitnessRig(double dt){
    // Store furniture anchors in the closed shape's local coordinates. Hinged
    // doors exchange their width and depth axes when they turn.
    const auto orient=[](Vec v,int id){
        return id==OpenDoor?Vec{v.z,v.y,v.x}:v;
    };
    contact::Queries scene;
    scene.cast=[&](Vec from,Vec dir,double reach)->std::optional<contact::Surface>{
        const auto hit=witnessRay(from,dir,reach);
        if(!hit)return {};
        const Vec normal=furniture(hit->id) || length(hit->surfaceNormal)>0.5?
            hit->surfaceNormal:hit->normal.v();
        contact::Surface surface{from+dir*hit->distance,normal};
        if(furniture(hit->id)){
            const int kind=furnitureBase(hit->id);
            const auto box=expedition::furnitureVolume(kind,hit->cell.v(),hit->id!=kind);
            Vec point=surface.point-box.lo;
            for(int axis=0;axis<3;axis++)point[axis]/=box.hi[axis]-box.lo[axis];
            surface.attachment=contact::Attachment{hit->cell,kind,orient(point,hit->id),orient(normal,hit->id)};
        }
        return surface;
    };
    scene.follow=[&](contact::Surface& surface){
        const auto& anchor=*surface.attachment;
        const int id=level<0?fixtureAt(anchor.cell):layer(family,level).raw(anchor.cell);
        if(furnitureBase(id)!=anchor.kind)return false;
        const auto box=expedition::furnitureVolume(anchor.kind,anchor.cell.v(),id!=anchor.kind);
        const Vec point=orient(anchor.point,id);
        for(int axis=0;axis<3;axis++)surface.point[axis]=box.lo[axis]+point[axis]*(box.hi[axis]-box.lo[axis]);
        surface.normal=orient(anchor.normal,id);
        return true;
    };
    const contact::Frame frame{witness.feet,witness.scale,witness.heading};
    const auto ceiling=witnessRay(frame.point({0,0.10,0}),{0,1,0},5*witness.scale);
    const double headroom=ceiling?0.10+ceiling->distance/witness.scale:5.1;
    // Lean only when the destination is reached. Check the head's lateral
    // sweep against current matter so a newly built wall cannot swallow it.
    Vec lean=witness.holdingCover?witness.coverLean:Vec{};
    if(length(lean)>.01){
        const Vec head=frame.point({-.08,1.68,1.06});
        if(witnessRay(head,unit(lean),(length(lean)+.23)*witness.scale))lean={};
    }
    witness.rig.peekOffset=lean;
    const auto& event=witness.horror;
    const int eaten=dread::eaten(event.age);
    witness.rig.feeding=event.mode==dread::Mode::Feed && event.age>=dread::FallDuration &&
        eaten<dread::Pieces && !witness.climbing && !witness.falling;
    witness.rig.feedingEffort=0.;
    if(witness.rig.feeding){
        const Vec morsel=preyPiecePosition(eaten);
        const double p=dread::bitePhase(event.age);
        witness.rig.feedingGrip=dread::grasp(p)*(1-dread::soft((p-.74)/.14));
        witness.rig.feedingEffort=.70*dread::feedPull(p)+.30*dread::feedSwallow(p);
        // Reach and lift toward the side of the jaw. IK remains in charge.
        // Relative to the current posed neck rather than a fixed height.
        // The side clearance lets the IK solver keep the arm outside the skull.
        const Vec neck=witness.rig.pose.neck;
        const Vec nearMouth=frame.point(neck+Vec{.52,-.20,.38});
        const Vec rest=frame.point({.82,.24,1.32});
        const Vec reach=morsel+Vec{0,.10*witness.scale,0};
        const Vec pick=rest+(reach-rest)*dread::soft(p/.18);
        const Vec raised=pick+(nearMouth-pick)*dread::lift(p);
        witness.rig.feedingTarget=raised+(rest-raised)*dread::lowerHand(p);
    }
    witness.rig.update(frame,witness.crouched,witness.climbing,witness.falling && !witness.climbing,
        witness.climbDirection,headroom,dt,scene);
    witness.groundedPose=std::any_of(witness.rig.limbs.begin(),witness.rig.limbs.end(),
        [](const contact::Limb& limb){return limb.phase==contact::Phase::Stance;});
    refreshWitnessMesh(dt);
}

namespace {
Vec rotatePart(Vec v,double yaw,double pitch=0){
    const double ct=std::cos(pitch),st=std::sin(pitch);
    const double cy=std::cos(yaw),sy=std::sin(yaw);
    const double z=v.z*ct-v.y*st;
    return {v.x*cy+z*sy,v.y*ct+v.z*st,-v.x*sy+z*cy};
}

bool sameSkinPose(const contact::Pose& a,const contact::Pose& b){
    // Below one millimeter at normal scale. Compare against the last pose
    // actually uploaded so slow motion accumulates rather than disappearing.
    const auto near=[](Vec p,Vec q){const auto d=p-q;return dot(d,d)<4e-7;};
    const auto unchanged=[&](const contact::Chain& p,const contact::Chain& q){
        return p.planted==q.planted && std::abs(p.grip-q.grip)<.001 && near(p.root,q.root) && near(p.joint,q.joint) &&
               near(p.tip,q.tip) && near(p.normal,q.normal) && near(p.forward,q.forward);
    };
    if(!near(a.pelvis,b.pelvis)||!near(a.chest,b.chest)||!near(a.neck,b.neck))return false;
    for(int i=0;i<2;i++)if(!unchanged(a.arms[i],b.arms[i])||!unchanged(a.legs[i],b.legs[i]))return false;
    return true;
}
}

void Game::refreshWitnessMesh(double dt){
    if(!witness.active || !witness.rig.ready)return;
    const auto& pose=witness.rig.pose;
    const auto skull=witness.feet+rotatePart(pose.neck,witness.heading)*witness.scale;
    // Track the last visible position when an obstacle interrupts sight. Eyes
    // acquire the target first; the heavier head follows with limited rotation.
    const auto& h=witness.horror;
    const bool feed=h.mode==dread::Mode::Feed;
    const bool examine=h.mode==dread::Mode::Inspect || h.mode==dread::Mode::Hunt;
    // During a bite, look down at the prey, then briefly look back at the
    // player between pieces. The existing eye/head follower still owns pose.
    const double bite=dread::bitePhase(h.age);
    const bool glance=feed && (dread::eaten(h.age)>=dread::Pieces || (dread::eaten(h.age)%2==1 && bite>.90)) && witness.sight.known;
    const Vec feedingLook=(feed && h.age>=dread::FallDuration && dread::eaten(h.age)<dread::Pieces)?
        preyPiecePosition(dread::eaten(h.age))*(1-dread::lift(bite))+witness.rig.frame.point(pose.arms[1].tip)*dread::lift(bite):h.goal+Vec{0,.18,0};
    const auto target=(feed && !glance)?feedingLook:examine?h.goal+Vec{0,.65,0}:
        witness.sight.known?witness.sight.predicted():skull+rotatePart({0,0,1},witness.heading)*witness.scale;
    const Vec ray=target-skull;
    const double bearing=std::atan2(ray.x,ray.z);
    const double elevation=std::atan2(ray.y,std::hypot(ray.x,ray.z));
    const double yawGoal=witness.heading+std::clamp(std::remainder(bearing-witness.heading,2*Pi),-1.22,1.22);
    // Asymmetric, infrequent head catches. The eye target is unchanged and
    // remains aimed at the last visible player position.
    const double catchMotion=std::pow(.5+.5*std::sin(witness.dreadPhase*1.17),18.0);
    // A slow downward chin tuck, with abrupt but limited catches; the eyes
    // keep their target rather than being dragged by the heavier skull.
    const double still=std::clamp(1.0-witness.pace,0.0,1.0);
    const double nod=modernWorld?still*(-.085+.045*std::sin(witness.dreadPhase*.39)+.19*catchMotion):0.0;
    const double pitchGoal=std::clamp(elevation+nod+witness.rage*(.17*std::sin(witness.dreadPhase*5.3)+.10*std::sin(witness.dreadPhase*11.7))+(feed && !glance?.16*dread::lift(bite)-.18*std::pow(std::max(0.,std::sin((bite-.57)*Pi*12)),3)*dread::lift(bite)*(1-dread::lowerHand(bite)):0.),-.64,.48);
    if(!witness.gazeReady){
        witness.gazeYaw=yawGoal;witness.gazePitch=pitchGoal;
        witness.eyeYaw=yawGoal;witness.eyePitch=pitchGoal;witness.gazeReady=true;
    }
    const auto follow=[dt](double angle,double goal,double rate,double maxSpeed){
        const double delta=std::remainder(goal-angle,2*Pi)*(1-std::exp(-dt*rate));
        return angle+std::clamp(delta,-maxSpeed*dt,maxSpeed*dt);
    };
    witness.gazeYaw=follow(witness.gazeYaw,yawGoal+witness.rage*.14*std::sin(witness.dreadPhase*7.1),(witness.observed?2.7:4.5)+5*witness.rage,2.4+3*witness.rage);
    witness.gazePitch=follow(witness.gazePitch,pitchGoal,3.2+5*witness.rage,1.75+3*witness.rage);
    const double eyeGoal=witness.gazeYaw+std::clamp(std::remainder(bearing-witness.gazeYaw,2*Pi),-0.44,0.44);
    witness.eyeYaw=follow(witness.eyeYaw,eyeGoal,23.0,8.0);
    witness.eyePitch=follow(witness.eyePitch,witness.gazePitch+std::clamp(elevation-witness.gazePitch,-0.31,0.31),20.0,6.0);

    const double distance=length(camera()-witness.feet)/witness.scale;
    const bool proxy=distance>(witnessProxy?20:23);
    const int oldBlood=bloodStage;refreshWitnessStains();
    if(lastBodyPose && proxy==witnessProxy && oldBlood==bloodStage && sameSkinPose(*lastBodyPose,pose))return;
    witnessSkins[proxy?1:0].pose(witnessBody.geometry,pose);
    const double stain=std::max(0,bloodStage)/8.;
    if(stain>0)for(auto& v:witnessBody.geometry.points){
        const double patch=.5+.5*std::sin(v.x*19+v.y*11)*std::sin(v.z*25-v.y*13);
        const float t=float(stain*dread::smooth((patch-.25)/.65)*.78);
        v.r=std::lerp(v.r,.24f,t);v.g=std::lerp(v.g,.032f,t);v.b=std::lerp(v.b,.025f,t);
    }
    witnessBody.measure();witnessBody.dirty=true;lastBodyPose=pose;witnessProxy=proxy;
}

void Game::refreshWitnessStains(){
    const int stage=std::clamp(int(std::ceil(witness.stain*8)),0,8);
    if(stage==bloodStage)return;
    bloodStage=stage;
    for(auto* part:{&witnessFace,&witnessJaw}){
        part->geometry=part==&witnessFace?cleanWitnessFace:cleanWitnessJaw;
        const double strength=stage/8.;
        for(auto& v:part->geometry.points){
            const double lower=1-dread::smooth((v.y+.12)/.26);
            const double patch=.5+.5*std::sin(v.x*46+v.z*29)*std::sin(v.y*32+v.x*21);
            const float t=float(strength*(.18+.82*lower)*dread::smooth((patch-.18)/.72));
            v.r=std::lerp(v.r,.29f,t);v.g=std::lerp(v.g,.027f,t);v.b=std::lerp(v.b,.018f,t);
        }
        part->dirty=true;part->upload();
    }
}
std::array<Game::WitnessPart,5> Game::witnessPose() const{
    const auto skull=witness.feet+rotatePart(witness.rig.pose.neck,witness.heading)*witness.scale;
    const auto socket=[&](int side){
        return skull+rotatePart(cranialCant(witnessSocket(side)),witness.gazeYaw,witness.gazePitch)*witness.scale;
    };
    const double jawPitch=witness.gazePitch+(witness.horror.mode==dread::Mode::Feed?dread::jawAngle(witness.horror.age):(-.12*witness.rage*std::pow(.5+.5*std::sin(witness.dreadPhase*8),6)));
    const Vec hinge=cranialCant({0,-.039,-.070});
    const Vec jawOrigin=skull+(rotatePart(hinge,witness.gazeYaw,witness.gazePitch)-
        rotatePart(hinge,witness.gazeYaw,jawPitch))*witness.scale;
    return {{{&witnessBody,witness.feet,witness.heading,0},
             {&witnessFace,skull,witness.gazeYaw,witness.gazePitch},
             {&witnessJaw,jawOrigin,witness.gazeYaw,jawPitch},
             {&witnessEyes[0],socket(-1),witness.eyeYaw,witness.eyePitch},
             {&witnessEyes[1],socket(1),witness.eyeYaw,witness.eyePitch}}};
}

void Game::drawWitness(){
    if(!witness.active || !witness.rig.ready || level!=witness.level || family!=witness.family)return;
    renderer.setNorthTexture();
    witnessBody.upload(true);
    for(const auto& part:witnessPose())
        renderer.draw(part.mesh->render,part.origin,witness.scale,part.heading,nullptr,
            (part.mesh==&witnessEyes[0] || part.mesh==&witnessEyes[1])?.12f:0.f,0,0,false,part.tilt);
}

double Game::witnessHit(Vec eye,Vec direction,double reach) const{
    if(!witness.active || !witness.rig.ready || level!=witness.level || family!=witness.family)return reach;
    const double scale=witness.scale;
    if(rayBox(eye,direction,witness.feet-Vec{5,3,5}*scale,
        witness.feet+Vec{5,7,5}*scale,reach)>=reach)return reach;
    const double limit=reach/scale;
    double end=limit;
    for(const auto& part:witnessPose()){
        const double cy=std::cos(part.heading),sy=std::sin(part.heading);
        const double ct=std::cos(part.tilt),st=std::sin(part.tilt);
        const auto local=[&](Vec point){
            const double x=cy*point.x-sy*point.z,z=sy*point.x+cy*point.z;
            return Vec{x,ct*point.y-st*z,st*point.y+ct*z};
        };
        const auto start=local(eye-part.origin)/scale;
        const auto ray=local(direction);
        const auto point=[](const Vertex& v){return Vec{v.x,v.y,v.z};};
        // Use the actual current mesh bounds only as a broad-phase filter;
        // the hit itself is always an intersection with a rendered triangle.
        if(rayBox(start,ray,part.mesh->bounds.lo,part.mesh->bounds.hi,end)>=end)continue;
        const auto& vertices=part.mesh->geometry.points;
        for(std::size_t i=0;i<vertices.size();i+=3){
            const auto a=point(vertices[i]),b=point(vertices[i+1]),c=point(vertices[i+2]);
            const Vec ab=b-a,ac=c-a,normal=cross(ray,ac);
            const double determinant=dot(ab,normal);
            if(std::abs(determinant)<1e-10)continue;
            const auto offset=start-a;
            const double u=dot(offset,normal)/determinant;
            if(u<0 || u>1)continue;
            const auto edge=cross(offset,ab);
            const double v=dot(ray,edge)/determinant;
            if(v<0 || u+v>1)continue;
            const double distance=dot(ac,edge)/determinant;
            if(distance>=0 && distance<end)end=distance;
        }
    }
    return end<limit?end*scale:reach;
}

Vec Game::northOrigin(Cell cell) const{
    if(northHome && *northHome==cell)return northFeet;
    Vec here=cell.v()+Vec{.5,0,.5};
    if(const auto* roof=world.roof(cell)){
        const double floor=roof->height(.5,.5);
        if(std::abs(floor-here.y)<.65)here.y=floor+.004;
    }
    return here;
}
// The mounted device remains North's unique persistent identity.
void Game::updateNorth(double dt){
    if(northHome){const auto it=devices.find(*northHome);
        if(it==devices.end() || it->second.id!=North){northHome.reset();northMoving=false;northWalk=northStride=0;}}
    if(!northHome){
        for(const auto& [cell,device]:devices)if(device.id==North){
            northFeet=northOrigin(cell);northHome=cell;northHeading=0;northStepClock=0;break;
        }
    }
    if(!northHome || level>=0 || body()<.25 || !wantsCapture())return;
    northStepClock+=std::clamp(dt,0.,.1);
    if(northStepClock<.05)return;
    const double step=std::min(.10,northStepClock);northStepClock=0;
    const Vec delta{feet.x-northFeet.x,0,feet.z-northFeet.z};
    const double distance=length(delta);
    if(distance>4.2)northMoving=true;
    if(distance<2.8)northMoving=false;
    double traveled=0;
    if(northMoving && distance>2.8){
        const Vec direction=unit(delta);
        const double speed=std::min(4.1,1.55+std::max(0.,distance-5)*.20);
        const double advance=std::min(distance-2.65,speed*step);
        const auto walk=[&](Vec d)->std::optional<Vec>{
            Vec next=northFeet+d*advance;
            const auto cell=floorCell(next);
            if(!world.tileReady(divFloor(cell.x,32),divFloor(cell.z,32)))return {};
            constexpr double size=1.6,height=1.46;
            Vec lifted=northFeet+Vec{0,.42,0};
            if(overlapsField(lifted,size,height))return {};
            for(int axis:{0,2}){
                const double amount=next[axis]-lifted[axis];
                if(std::abs(sweepField(lifted,size,axis,amount,height)-amount)>1e-5)return {};
                lifted[axis]+=amount;
            }
            const double down=sweepField(lifted,size,1,-1.10,height);
            if(down<=-1.099)return {};
            next=lifted+Vec{0,down+.008,0};
            if(overlapsField(next,size,height) || world.sample(0,floorCell(next+Vec{0,.1,0})).id==Water)return {};
            return next;
        };
        for(double angle:{0.,.55,-.55,1.05,-1.05,1.52,-1.52}){
            const Vec d=direction*std::cos(angle)+Vec{direction.z,0,-direction.x}*std::sin(angle);
            if(auto next=walk(d)){
                traveled=length(Vec{next->x-northFeet.x,0,next->z-northFeet.z});northFeet=*next;break;
            }
        }
    }
    northStride=std::fmod(northStride+traveled*9.0,2*Pi);
    northWalk=std::lerp(northWalk,std::clamp(traveled/(step*1.55),0.,1.),1-std::exp(-step*10));
    if(distance>.05){const double desired=std::atan2(delta.x,delta.z);
        northHeading+=std::remainder(desired-northHeading,2*Pi)*(1-std::exp(-step*6));}
}
int Game::prepareNorth(double range){
    const int lod=range>17?1:0;
    const int tick=int(std::fmod(playTime,67000.)*(lod?10:30));
    if(northPoseTick[lod]!=tick){northRigs[lod].pose(northPosed,double(tick)/(lod?10:30),northStride,northWalk,true,true);
        northModels[lod].uploadIndexed(northPosed,northRigs[lod].indices);northPoseTick[lod]=tick;}
    return lod;
}
void Game::drawNorth(const View& view){
    for(const auto& [cell,device]:devices){
        if(device.id!=North)continue;
        const Vec here=northOrigin(cell);
        if(!view.sees({here-Vec{0.75,0.20,0.70},here+Vec{0.75,2.5,0.70}},double(fieldWidth)/fieldHeight,view.fog))continue;
        const int lod=prepareNorth(length(here-view.camera));
        const double heading=northHome && *northHome==cell?northHeading:std::atan2(view.camera.x-here.x,view.camera.z-here.z);
        renderer.draw(northModels[lod],here,1,heading);
    }
}

// Small, resident surface wildlife. Population is seed-stable; only nearby
// loaded tiles are inspected, and consumed animals stay gone across saves.
void Game::makeHorrorModels(){
    const Color fur{.68f,.43f,.25f},dark{.13f,.12f,.10f};
    for(int pose=0;pose<int(deerModels.size());pose++){
        Geometry deer;woodland::deer(deer,pose==0?-1:2*Pi*(pose-1)/(deerModels.size()-1));
        deerModels[pose].upload(deer);
    }
    for(int i=0;i<FallFrames;i++){
        const double t=double(i)/(FallFrames-1);Geometry deer;woodland::deer(deer,-1,dread::smooth(t));
        fallenDeerFloor[i]=dread::supportHeight(deer.points,dread::fallAngle(t));fallenDeer[i].upload(deer);
    }
    // Separate closed meshes for the body pieces. The detached material is
    // stylized, with torn cloth/fur and dark cut surfaces; no flat decals.
    for(int kind=0;kind<7;kind++)for(int part=0;part<dread::Pieces;part++){
        Geometry piece;
        const bool animal=kind==0,head=part==0,bodyPart=part==1;
        const Color hide=animal?fur:(kind==1||kind==6)?Color{.44f,.54f,.28f}:Color{.75f,.71f,.66f};
        const std::array<Color,4> elfCloth={Color{.39f,.82f,.63f},Color{.58f,.47f,1.03f},Color{.94f,.69f,.31f},Color{.44f,.75f,1.02f}};
        const Color cloth=animal?fur:kind==1?Color{.33f,.23f,.39f}:kind==6?Color{.36f,.14f,.11f}:elfCloth[kind-2];
        const Color torn{.29f,.055f,.045f},bone{.64f,.57f,.42f};
        if(head){
            piece.stone({},animal?.15:.16,animal?ClothTexture:SkinTexture,hide,9,14);
            piece.tube({0,-.04,.05},{0,-.06,.22},animal?.075:.07,ClothTexture,hide,10);
            for(int side:{-1,1}){
                piece.tube({side*.09,.06,0},{side*.22,.15,-.04},.036,animal?ClothTexture:SkinTexture,hide,9);
                piece.stone({side*.09,.028,.119},.018,LeatherTexture,dark,5,8);
            }
            piece.stone({0,-.126,0},.065,SkinTexture,torn,5,9);
        }else if(bodyPart){
            piece.tube({0,0,-.20},{0,0,.20},animal?.23:.19,animal?ClothTexture:ClothTexture,cloth,14);
            piece.stone({0,0,-.20},.16,SkinTexture,torn,6,10);
            for(int i=0;i<3;i++)piece.tube({-.095+i*.08,0,-.21},{-.09+i*.08,.015,-.29},.014,WitnessBoneTexture,bone,7);
            piece.stone({0,0,.20},.17,animal?ClothTexture:ClothTexture,cloth,6,12);
        }else{
            const Vec a{0,0,-.20},b{.045,.02,.015},c{-.02,0,.19};
            piece.tube(a,b,animal?.057:.075,animal?ClothTexture:ClothTexture,cloth,10);
            piece.tube(b,c,.045,animal?ClothTexture:SkinTexture,hide,9);
            piece.stone(a,.051,SkinTexture,torn,5,9);
            piece.tube(a,a+Vec{0,0,-.07},.018,WitnessBoneTexture,bone,8);
            piece.stone(c,.053,LeatherTexture,dark,5,8);
        }
        preyPieces[kind][part].upload(piece);
    }
    Geometry drop,remnant;
    drop.stone({},.035,FeedingBloodTexture,{.45f,.026f,.019f},5,8);bloodDrop.upload(drop);
    for(int j=0;j<9;j++){
        const double a=j*2.39996323;
        remnant.stone({.05*std::cos(a),.012+.008*(j%3),.05*std::sin(a)},.032+.009*(j%3),FeedingBloodTexture,{.31f,.025f,.018f},4,7);
    }
    for(auto& v:remnant.points){v.y*=.37f;v.ny/=.37f;const Vec n=unit({v.nx,v.ny,v.nz});v.nx=n.x;v.ny=n.y;v.nz=n.z;}
    bloodRemnant.upload(remnant);
    Geometry flash;
    flash.stone({},.22,Lamp,{1.6f,1.8f,2.f},8,14);
    for(int i=0;i<9;i++){
        const double a=i*2.3999632297;
        const Vec d=unit({std::cos(a),std::sin(a)*.65,std::sin(a*1.7)});
        flash.tube(d*.12,d*(.34+.05*(i%3)),.013,Lamp,{.70f,1.4f,1.7f},5);
    }
    witnessImpact.upload(flash);
}

void Game::updateForestLife(double dt){
    if(!modernWorld || !world.seed || level>=0 || body()<.25)return;
    deerMotion+=dt;
    if(deerMotion<.05)return;
    dt=std::min(.10,deerMotion);deerMotion=0;
    const Cell center=parent(floorCell(feet),32);
    const auto ground=[&](Vec p)->std::optional<Vec>{
        const auto cell=floorCell(p);
        if(!world.tileReady(divFloor(cell.x,32),divFloor(cell.z,32)))return {};
        const auto* roof=world.roof(cell);if(!roof)return {};
        const double h=roof->height(p.x-cell.x,p.z-cell.z);
        if(roof->normal(p.x-cell.x,p.z-cell.z).y<.75)return {};
        const Vec probe{p.x,h+.55,p.z};
        if(world.overlaps(probe,1.5,1.2))return {};
        const double down=world.sweep(probe,1.5,1,-1.1,1.2);
        if(down<=-1.099)return {};
        p=probe+Vec{0,down+.005,0};
        if(world.sample(0,floorCell(p+Vec{0,.14,0})).id==Water)return {};
        return p;
    };
    deerScan-=dt;
    if(deerScan<=0){
        deerScan=1.3;
        std::erase_if(forestDeer,[&](const auto& entry){
            const auto& k=entry.first;
            return std::abs(k.x-center.x)>2 || std::abs(k.z-center.z)>2;
        });
        for(int dz=-1;dz<=1;dz++)for(int dx=-1;dx<=1;dx++){
            const Cell tile{center.x+dx,0,center.z+dz};
            if(tile.x< -64 || tile.x>=64 || tile.z< -64 || tile.z>=64 || forestDeer.contains(tile)||consumedDeer.contains(tile)||!world.tileReady(tile.x,tile.z))continue;
            const auto hash=mix(std::uint32_t(*world.seed)^std::uint32_t(CellHash{}(tile)));
            if(!settlement::rareDeer(hash) || !forestDeer.empty())continue;
            for(int attempt=0;attempt<8;attempt++){
                const auto k=mix(hash+attempt*3781u);
                Vec p{tile.x*32.+5+(k&255)/255.*22,0,tile.z*32.+5+((k>>8)&255)/255.*22};
                auto pos=ground(p);if(!pos || length(*pos-feet)<9 || length(*pos-feet)>48)continue;
                if(witness.active && length(*pos-witness.feet)<6)continue;
                // Don't spawn a new creature in full view of the player.
                const Vec to=*pos+Vec{0,1,0}-camera();
                if(dot(unit(to),viewDirection(yaw,pitch))>.72 && !witnessRay(camera(),unit(to),length(to)-.3))continue;
                forestDeer.emplace(tile,Deer{*pos,2*Pi*((k>>16)&255)/255.,0,2.0});break;
            }
        }
    }
    for(auto& [id,deer]:forestDeer){
        if(length(deer.feet-feet)>55)continue;
        // Gravity remains authoritative even if the player mines the ground
        // underneath a resting animal. Only loaded columns participate.
        const auto column=floorCell(deer.feet);
        if(!world.tileReady(divFloor(column.x,32),divFloor(column.z,32)))continue;
        deer.fallSpeed=std::min(12.,deer.fallSpeed+dt*18);
        const double fall=-deer.fallSpeed*dt;
        const double moved=world.sweep(deer.feet,1.5,1,fall,1.2);
        deer.feet.y+=moved;
        if(moved>fall+1e-5)deer.fallSpeed=0;
        if(deer.fallSpeed>.1)continue;
        const Vec away=deer.feet-witness.feet;
        const bool fear=witness.active && witness.level==level && witness.family==family && length(away)<11;
        const bool startled=length(deer.feet-feet)<3.2;
        deer.idle-=dt;
        if(fear || startled){
            const Vec escape=fear?away:deer.feet-feet;
            deer.heading=std::atan2(escape.x,escape.z);
            deer.idle=-1;
        }else if(deer.idle<-2){
            deer.heading+=.9*std::sin(deer.stride+id.x*2.1+id.z*1.3);
            deer.idle=3.+std::fmod(std::abs(deer.stride),3.);
        }
        if(deer.idle>0)continue;
        const double speed=fear?2.15:startled?1.9:.65;
        const Vec forward{std::sin(deer.heading),0,std::cos(deer.heading)};
        for(double angle:{0.,.7,-.7,1.4,-1.4}){
            const Vec heading=forward*std::cos(angle)+Vec{forward.z,0,-forward.x}*std::sin(angle);
            const Vec p=deer.feet+heading*(speed*dt);
            if(length(p-id.v()*32-Vec{16,p.y,16})>27)continue;
            const auto next=ground(p);
            if(!next || std::abs(next->y-deer.feet.y)>.25)continue;
            deer.feet=*next;deer.heading=std::atan2(heading.x,heading.z);deer.stride+=speed*dt*9;break;
        }
    }
}

bool Game::refreshPrey(dread::Prey& p){
    switch(p.kind){
        case dread::Kind::Deer:{
            if(level>=0)return false;
            auto it=forestDeer.find(p.tile);if(it==forestDeer.end())return false;
            p.feet=it->second.feet;p.yaw=it->second.heading;return true;
        }
        case dread::Kind::Goblin:{
            if(!modernWorld || !settlement::site(level) || p.index<0 || p.index>=int(guards.size()) || !guardHere(p.index))return false;
            auto& guard=goblin(localGuard(p.index));if(guard.health<=0)return false;
            p.feet=guard.feet;p.yaw=guard.yaw;return true;
        }
        case dread::Kind::Crawler:
        case dread::Kind::Elf:{
            if(level<0)return false;
            auto& a=wildlife(family,level);
            if(p.kind==dread::Kind::Elf){
                if(level!=expedition::Village || p.index<1 || p.index>=int(a.elves.size()) || a.missingElves.any())return false;
                p.feet=a.elves[p.index].feet;p.yaw=a.elves[p.index].yaw;
            }else{
                if(p.index<0 || p.index>=expedition::Hostiles || a.defeated[p.index])return false;
                p.feet=a.enemies[p.index].feet;p.yaw=a.enemies[p.index].yaw;
            }
            return true;
        }
        default:return false;
    }
}
std::optional<dread::Prey> Game::findPrey(Vec head,bool goblinsOnly){
    std::optional<dread::Prey> best;double score=22*witness.scale;
    const Vec player=level<0?feet:innerFeet;
    const auto consider=[&](dread::Prey p){
        if(!refreshPrey(p))return;
        const Vec to=p.feet+Vec{0,p.kind==dread::Kind::Goblin?1.25:.9,0}-head;const double distance=length(to);
        const bool guard=p.kind==dread::Kind::Goblin;
        if(distance>score || length(p.feet-player)>(guard?30:23)*witness.scale || (!guard && length(p.feet-player)<2.5*witness.scale))return;
        if(distance>1e-6 && witnessRay(head,unit(to),std::max(0.,distance-.25)))return;
        score=distance;best=p;
    };
    if(modernWorld && settlement::site(level)){
        for(int role=0;role<guardCount();role++)consider({dread::Kind::Goblin,{},guardId(role)});
        if(best)return best;
    }
    if(goblinsOnly || inFortress())return {};
    if(level<0){for(const auto& [id,deer]:forestDeer)consider({dread::Kind::Deer,id,-1,deer.feet,deer.heading});}
    else{
        auto& a=wildlife(family,level);
        for(int i=0;i<expedition::Hostiles;i++)if(!a.defeated[i])consider({dread::Kind::Crawler,{},i});
        // Aurel remains protected, as do at least three of the four named
        // villagers. Never destroy every friendly interaction in a world.
        if(level==expedition::Village && !a.missingElves.any())for(int i=1;i<4;i++)consider({dread::Kind::Elf,{},i});
    }
    return best;
}
bool Game::consumePrey(dread::Prey& p){
    if(!refreshPrey(p))return false;
    if(p.kind==dread::Kind::Deer){consumedDeer.insert(p.tile);forestDeer.erase(p.tile);}
    else if(p.kind==dread::Kind::Elf)wildlife(family,level).missingElves.set(p.index);
    else if(p.kind==dread::Kind::Crawler)wildlife(family,level).defeated.set(p.index);
    else if(p.kind==dread::Kind::Goblin){auto& guard=guards[p.index];guard.health=0;guard.beginCollapse(dot(unit(guard.feet-witness.feet),{std::cos(guard.yaw),0,-std::sin(guard.yaw)}));guard.fallenToWitness=true;}
    else return false;
    witness.stain=std::min(1.,witness.stain+.21);
    rememberedVoice=p.voice();learnedVoices.set(rememberedVoice);
    const Vec offset=unit(p.feet-camera()),right=unit(cross(viewDirection(yaw,pitch),{0,1,0}));
    creatureVoice(rememberedVoice,false,dot(offset,right),.78);
    return true;
}
void Game::noticeWitness(Vec point){
    if(!modernWorld || !witness.active || witness.family!=family || witness.level!=level ||
        length(point-witness.feet)>24*witness.scale)return;
    const Vec head=witness.rig.ready?witness.rig.frame.point(witness.rig.pose.neck):witness.feet+Vec{0,1.7*witness.scale,0};
    const Vec ray=point-head;
    if(length(ray)>witness.scale && witnessRay(head,unit(ray),std::max(0.,length(ray)-.6*witness.scale)))return;
    witness.horror.notice(point);
}

bool Game::horrorPlan(double dt,bool watched,bool hasLine,Vec eye,Vec head,
    const std::function<bool(Vec,double)>& fits,const std::function<Vec(Vec)>& grounded,
    const std::function<bool(Vec,Vec)>& obstructed){
    (void)hasLine;
    auto& h=witness.horror;
    const double scale=witness.scale,range=length(witness.feet-eye)/scale;
    using dread::Mode;
    h.tick(dt);
    const auto finish=[&](double delay){h.finish(delay);witness.detourLeft=0;witness.travelDirection={};};
    const auto begin=[&](Mode mode,Vec goal){h.enter(mode);h.goal=goal;witness.detourLeft=0;witness.travelDirection={};witness.blockedTime=0;};
    const auto hiding=[&](Vec p){
        return obstructed(eye,p+Vec{0,1.05*scale,0}) && obstructed(eye,p+Vec{0,1.85*scale,0});
    };
    // Query budget is finite and only spent at the start of an encounter.
    const auto retreatSite=[&](bool hidden)->std::optional<Vec>{
        const Vec away=unit(Vec{witness.feet.x-eye.x,0,witness.feet.z-eye.z});
        const Vec side{away.z,0,-away.x};
        double best=-1e9;std::optional<Vec> result;
        for(int i=0;i<16;i++){
            const double angle=(i/2)*Pi/7*(i%2?1:-1);
            const Vec dir=away*std::cos(angle)+side*std::sin(angle);
            Vec p=witness.feet+dir*(scale*(5+(i%3)*2));
            if(level<0 && world.seed){
                const auto* roof=world.roof(floorCell(p));if(!roof)continue;
                const double surface=roof->height(p.x-std::floor(p.x),p.z-std::floor(p.z));
                if(std::abs(surface-p.y)<5*scale)p.y=surface;
            }
            if(length(p-eye)<6*scale || length(p-eye)>26*scale || hiding(p)!=hidden)continue;
            p=grounded(p);if(!fits(p,2.25)||hiding(p)!=hidden)continue;
            if(!witnessRay(p+Vec{0,.09*scale,0},{0,-1,0},.24*scale))continue;
            if(!hidden && obstructed(eye,p+Vec{0,1.85*scale,0}))continue;
            const Vec old=unit(Vec{h.retreatOrigin.x-eye.x,0,h.retreatOrigin.z-eye.z});
            const Vec reveal=unit(Vec{p.x-eye.x,0,p.z-eye.z});
            const double angleChange=1-dot(old,reveal);
            const double value=hidden?dot(dir,away)*2.-length(p-witness.feet)/scale*.11:
                angleChange*6.+std::abs(dot(dir,side))*2.-length(p-eye)/scale*.06;
            if(value>best){best=value;result=p;}
        }
        return result;
    };
    if(h.mode!=Mode::Watch && (range>48 || (witness.blockedTime>4.5 && h.mode!=Mode::Feed) || (witness.falling && h.mode==Mode::Feed))){finish(12);return false;}
    if(h.mode==Mode::Watch){
        if(h.scan>0 || range>32 || witness.climbing || witness.falling)return false;
        h.scan=.8;
        if(scale>=.5){if(auto p=findPrey(head,true);p && (h.cooldown<=0 || (h.cooldown>12 && dread::horizontal(p->feet-witness.feet)<5*scale))){
            h.prey=*p;begin(Mode::Hunt,p->feet);return true;
        }}
        if(h.cooldown>0 || range>24 || range<4)return false;
        h.scan=2.5;
        if(auto p=(scale>=.5)?findPrey(head):std::optional<dread::Prey>{};p){
            h.prey=*p;begin(Mode::Hunt,p->feet);return true;
        }
        if(h.interesting() && length(h.interest-witness.feet)<18*scale && length(h.interest-eye)>3.2*scale){
            h.interestKnown=false;begin(Mode::Inspect,h.interest);return true;
        }
        if(watched && witness.stareTime>.65){
            if(auto p=retreatSite(true)){h.retreatOrigin=witness.feet;begin(Mode::Retreat,*p);return true;}
        }
        return false;
    }
    if(h.mode==Mode::Hunt){
        if(h.age>35 || !refreshPrey(h.prey)){finish(16);return false;}
        h.goal=h.prey.feet;
        const double distance=dread::horizontal(h.goal-witness.feet);
        const Vec target=h.prey.feet+Vec{0,h.prey.kind==dread::Kind::Goblin?1.15:.75,0};
        const bool guard=h.prey.kind==dread::Kind::Goblin;
        const bool inReach=distance<(guard?2.65:2.15)*scale+.25 && std::abs(h.goal.y-witness.feet.y)<1.65*scale && !obstructed(head,target);
        h.engageAge=inReach?h.engageAge+dt:0;
        const bool ready=!guard || h.engageAge>1.30;
        if(ready && inReach){
            if(consumePrey(h.prey)){begin(Mode::Feed,h.prey.feet);h.mimicTimer=8;return true;}
            finish(15);return false;
        }
    }else if(h.mode==Mode::Feed){
        for(int i=0;i<dread::Pieces;i++){
            auto& drop=h.droppings[i];
            if(drop.ready || h.age<dread::releaseTime(i))continue;
            const Vec skull=witness.feet+rotatePart(witness.rig.pose.neck,witness.heading)*scale;
            drop.from=skull+rotatePart(cranialCant({0,-.22,.20}),witness.gazeYaw,witness.gazePitch)*scale;
            const double a=h.prey.yaw+i*2.39996323;
            Vec landing=drop.from+Vec{.16*std::sin(a)*scale,0,.16*std::cos(a)*scale};
            if(auto hit=witnessRay(landing,{0,-1,0},5*scale)){
                drop.to=landing+Vec{0,-hit->distance+.018*scale,0};drop.ready=true;
                witness.stain=std::min(1.,witness.stain+.065);
            }
        }
        if(h.age>=dread::FeedDuration){const bool guard=h.prey.kind==dread::Kind::Goblin;finish(guard?2.5:35);return false;}
    }else if(h.mode==Mode::Retreat){
        if(hiding(witness.feet) && dread::horizontal(h.goal-witness.feet)<1.2*scale){begin(Mode::Concealed,witness.feet);return true;}
        if(h.age>13){finish(15);return false;}
    }else if(h.mode==Mode::Concealed){
        if(!hiding(witness.feet)){finish(14);return false;}
        if(h.age>6){finish(18);return false;}
        if(h.age>2.8 && h.scan<=0){
            h.scan=1.2;
            if(auto p=retreatSite(false)){begin(Mode::Reappear,*p);return true;}
        }
    }else if(h.mode==Mode::Reappear){
        if(h.age>13 || dread::horizontal(h.goal-witness.feet)<.8*scale){finish(22);return false;}
    }else if(h.mode==Mode::Inspect){
        if(h.age>12){finish(14);return false;}
    }
    return true;
}

void Game::updateHorrorAudio(double dt){
    const bool enabled=modernWorld && combatEnabled && wantsCapture();
    const bool present=witness.active && witness.family==family && witness.level==level;
    const double range=present?length(witness.feet-camera())/witness.scale:1000;
    ambientGain=dread::quietStep(ambientGain,present?dread::quietTarget(range):1.,dt);
    forestAudio(enabled,ambientGain,night);
    if(!enabled)return;
    auto& h=witness.horror;
    const Vec right=unit(cross(viewDirection(yaw,pitch),{0,1,0}));
    if(present && learnedVoices.any() && h.mimicTimer<=0 && range<27 &&
       (h.mode==dread::Mode::Concealed || h.mode==dread::Mode::Watch) && !witness.observed){
        std::array<int,6> choices{};int count=0;
        for(int i=0;i<6;i++)if(learnedVoices[i] && (i!=lastMimicVoice || learnedVoices.count()==1))choices[count++]=i;
        const auto k=mix(std::uint32_t(CellHash{}(floorCell(witness.feet)))^std::uint32_t(witness.dreadPhase*20));
        lastMimicVoice=choices[k%count];
        creatureVoice(lastMimicVoice,true,dot(unit(witness.feet-camera()),right),.72,lastMimicVoice<4);
        h.mimicTimer=18+double(CellHash{}(floorCell(witness.feet))%13);
    }
    ambientCall-=dt;
    if(ambientCall<=0 && ambientGain>.8){
        ambientCall=10;
        if(level<0){
            for(const auto& [id,deer]:forestDeer){
                const Vec to=deer.feet-camera();if(length(to)<24){creatureVoice(4,false,dot(unit(to),right),.28);break;}
            }
        }else if(!inFortress()){
            const auto& a=wildlife(family,level);
            int id=-1;Vec nearest{};double distance=14;
            if(level==expedition::Village)for(int i=0;i<4;i++)if(!a.missingElves[i]){
                const Vec to=a.elves[i].feet-camera();if(length(to)<distance){id=i;nearest=to;distance=length(to);}
            }
            for(int i=0;i<expedition::Hostiles;i++)if(!a.defeated[i]){
                const Vec to=a.enemies[i].feet-camera();if(length(to)<distance){id=5;nearest=to;distance=length(to);}
            }
            if(id>=0)creatureVoice(id,false,dot(unit(nearest),right),.26);
        }
    }
}
Vec Game::preyPiecePosition(int part,std::optional<Vec> bodyBase){
    const auto& h=witness.horror;
    const double angle=h.prey.yaw+part*2.3999632297;
    if(h.prey.kind!=dread::Kind::Goblin)return h.prey.feet+Vec{std::sin(angle)*(.15+.075*part),.13,std::cos(angle)*(.15+.075*part)};
    const int role=guardClassForId(h.prey.index);
    constexpr std::array<int,dread::Pieces> order{2,3,4,5,1,0};
    const Vec base=rotatePart(guardianPieceCenter[role][order[part]],h.prey.yaw)*settlement::GuardScale;
    return (bodyBase?*bodyBase:goblinCorpseBase(h.prey.feet,h.prey.yaw,role,1.))+base;
}
void Game::drawHorror(const View& view){
    if(!modernWorld)return;
    if(level<0)for(const auto& [id,deer]:forestDeer){
        if(!view.sees({deer.feet-Vec{.4,0,.8},deer.feet+Vec{.5,2.05,1.0}},double(fieldWidth)/fieldHeight,60))continue;
        const int step=deer.idle>0?0:1+int(std::fmod(std::max(0.,deer.stride),2*Pi)/(2*Pi)*(deerModels.size()-1))%(deerModels.size()-1);
        renderer.draw(deerModels[step],deer.feet,1,deer.heading);
    }
    if(!witness.active || !witness.rig.ready || witness.family!=family || witness.level!=level || witness.horror.mode!=dread::Mode::Feed)return;
    const auto& h=witness.horror;
    const bool guard=h.prey.kind==dread::Kind::Goblin;
    const int role=guard?guardClassForId(h.prey.index):0;
    const int kind=h.prey.kind==dread::Kind::Deer?0:h.prey.kind==dread::Kind::Crawler?1:guard?6:2+std::clamp(h.prey.index,0,3);
    const int lod=length(view.camera-h.prey.feet)>(renderer.graphics().detail<2?5.5:9.)?1:0;
    if(h.age<dread::FallDuration && (guard || kind==0)){
        const double t=std::clamp(h.age/dread::FallDuration,0.,1.);
        const int frame=std::clamp(int(std::round(t*(FallFrames-1))),0,FallFrames-1);
        const double size=guard?settlement::GuardScale:1.;
        if(guard){
            if(!view.sees({h.prey.feet-Vec{3,1,3},h.prey.feet+Vec{3,3,3}},double(fieldWidth)/fieldHeight,view.fog))return;
            drawGoblinCollapse(guards[h.prey.index],h.prey.feet,h.prey.yaw,role*2+lod,t);
        }else{
            const double angle=dread::fallAngle(double(frame)/(FallFrames-1));
            const double support=fallenDeerFloor[frame];
            renderer.draw(fallenDeer[frame],h.prey.feet+Vec{0,support*size+.005,0},size,h.prey.yaw,nullptr,0,0,0,false,angle);
        }
        return;
    }
    const int done=dread::eaten(h.age);
    const Vec mouth=witness.rig.frame.point(witness.rig.pose.neck)+
        rotatePart(cranialCant({0,-.22,.20}),witness.gazeYaw,witness.gazePitch)*witness.scale;
    // Each completed bite sheds a short ballistic spray and leaves a small
    // compressed remnant on supported terrain below the mouth.
    for(int i=0;i<=std::min(done,dread::Pieces-1);++i){
        const double elapsed=h.age-dread::releaseTime(i);
        if(elapsed<0)continue;
        const auto& drop=h.droppings[i];if(!drop.ready)continue;
        const double a=h.prey.yaw+i*2.39996323;
        const double flight=std::sqrt(std::max(.01,2*(drop.from.y-drop.to.y)/(9.8*witness.scale)));
        const double t=std::clamp(elapsed/flight,0.,1.);
        Vec remnant=drop.from+(drop.to-drop.from)*t;remnant.y=std::lerp(drop.from.y,drop.to.y,t*t);
        renderer.draw(bloodRemnant,remnant,(guard?1.3:1.)*witness.scale,a,nullptr,0,0,0,false,(1-t)*elapsed*6.);
        if(t<1)for(int j=0;j<10;j++){
            const double b=a+j*2.39996323;
            Vec pos=drop.from+Vec{std::cos(b)*(.28+.11*(j%4)),.10+.12*(j%3),std::sin(b)*(.28+.11*(j%4))}*elapsed*witness.scale;
            pos.y-=4.9*witness.scale*elapsed*elapsed;
            if(pos.y>drop.to.y+.025*witness.scale)renderer.draw(bloodDrop,pos,(.65+.2*(j%3))*witness.scale,b);
        }
    }
    const auto bodyBase=guard?std::optional<Vec>{goblinCorpseBase(h.prey.feet,h.prey.yaw,role,1.)}:std::nullopt;
    for(int i=done;i<dread::Pieces;i++){
        const double angle=h.prey.yaw+i*2.3999632297;
        Vec at=preyPiecePosition(i,bodyBase);
        double size=guard?settlement::GuardScale:1.;
        if(i==done){
            const double p=dread::bitePhase(h.age);
            const Vec hand=witness.rig.frame.point(witness.rig.pose.arms[1].tip);
            const double grab=dread::grasp(p);
            at=at+(hand-at)*grab;
            // Keep the piece with the solved hand until the jaw closes.
            at=at+(mouth-at)*dread::soft((p-.55)/.15);
            size*=1-dread::swallow(p);
            if(p>=.85)continue;
        }
        constexpr std::array<int,dread::Pieces> order{2,3,4,5,1,0};
        renderer.draw(guard?guardianPieces[role*2+lod][order[i]]:preyPieces[kind][i],at,size,guard?h.prey.yaw:angle,nullptr,0,0,0,false,i==done?.65*dread::lift(dread::bitePhase(h.age))+.16*dread::feedPull(dread::bitePhase(h.age))-.24*dread::swallow(dread::bitePhase(h.age)):0.);
    }
}
void Game::flashLight(std::array<Light,8>& lights) const{
    for(const auto& bolt:bolts)if(bolt.absorbed && bolt.family==family && bolt.level==level && bolt.age>=.065 && bolt.age<.19){
        const float strength=float(2.8*(1-(bolt.age-.065)/.125));
        auto slot=std::find_if(lights.begin(),lights.end(),[](const Light& light){return light.strength<=0;});
        if(slot==lights.end())slot=lights.end()-1;
        *slot={bolt.to,strength};break;
    }
}
