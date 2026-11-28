#include "game.hpp"
#include "natural-world.hpp"

int Game::goblinSlot() const {
    return level<0?0:1+family*micro::Count+level;
}

Vec Game::guardChest() const{
    Vec point=settlement::site(level)->chest;
    if(level<0 && world.seed && world.sculptedTerrain){
        if(const auto* roof=world.roof(floorCell(point)))point.y=roof->height(point.x-std::floor(point.x),point.z-std::floor(point.z))+.004;
        else point.y=natural::surfaceHeight(point.x,point.z,*world.seed)+.004;
    }
    return point;
}

settlement::Guard& Game::goblin(int role){
    auto& guard=guards[guardId(role)];
    if(!guard.ready){
        guard.ready=true;
        if(inFortress()){
            guard.feet=settlement::FortressPosts[role];guard.alert=true;
            guard.yaw=std::atan2(24.5-guard.feet.x,28.-guard.feet.z);
            guard.attackWait=.25+.11*role;
            return guard;
        }
        guard.feet=settlement::site(level)->guard+Vec{0,0.001,0};
        if(role==1)guard.feet+=Vec{1.8,0,-2.2};
        if(role==2)guard.feet+=Vec{-1.3,0,2.6};
        if(level<0 && world.seed && world.sculptedTerrain){
            const Vec home=guard.feet;
            for(int i=0;i<13;i++){
                Vec p=home+(i?Vec{std::sin(i*2.4),0,std::cos(i*2.4)}*(.6+i*.22):Vec{});
                if(const auto* roof=world.roof(floorCell(p)))p.y=roof->height(p.x-std::floor(p.x),p.z-std::floor(p.z))+.005;
                else continue;
                if(!world.overlaps(p,settlement::GuardScale,1.44)){guard.feet=p;break;}
            }
        }
    }
    return guard;
}

void Game::makeGoblinModels(){
    for(int role=0;role<settlement::GuardClasses;role++)for(int lod=1;lod>=0;lod--){
        const int i=role*2+lod;
        Geometry guard;
        goblinRigs[i].build(lod!=0,role);
        goblinRigs[i].pose(guard,0,0,0);
        goblinModels[i].upload(guard);
        // Store only settled poses, rather than seven copies of the collapse.
        Geometry fallen;goblinRigs[i].pose(fallen,0,0,0,1);
        fallenGoblinFloor[i]=dread::supportHeight(fallen.points,dread::fallAngle(1));
        fallenGoblin[i].upload(fallen);
        const double angle=dread::fallAngle(1),c=std::cos(angle),s=std::sin(angle);
        for(int part=0;part<dread::Pieces;part++){
            Geometry piece;goblinRigs[i].pose(piece,0,0,0,1,0,-1,part);
            Vec lo{1e9,1e9,1e9},hi{-1e9,-1e9,-1e9};
            Vec contact{0,1e9,0};
            for(auto& v:piece.points){
                const double y=v.y*c+v.z*s,z=-v.y*s+v.z*c,ny=v.ny*c+v.nz*s,nz=-v.ny*s+v.nz*c;
                v.y=y+fallenGoblinFloor[i]+.005/settlement::GuardScale;v.z=z;v.ny=ny;v.nz=nz;
                const Vec p{v.x,v.y,v.z};
                if(p.y<contact.y)contact=p;
                for(int a=0;a<3;a++){lo[a]=std::min(lo[a],p[a]);hi[a]=std::max(hi[a],p[a]);}
            }
            // Shared anchors prevent offsets when changing LOD.
            if(lod==1){
                guardianPieceCenter[role][part]=(lo+hi)*.5;
                fallenGoblinContacts[role][part]=contact;
            }
            const Vec center=guardianPieceCenter[role][part];
            for(auto& v:piece.points){v.x-=center.x;v.y-=center.y;v.z-=center.z;}
            guardianPieces[i][part].upload(piece);
        }
    }
    for(int i=0;i<2;i++){Geometry cache;settlement::chest(cache,i!=0);chestModels[i].upload(cache);}
}

bool Game::goblinStrike(settlement::Guard& guard,int role){
    const int kind=guardClass(role);
    return strikeWitness(guard.feet+Vec{0,1.18*settlement::GuardScale,0},
        inFortress() && role<4?13.:settlement::attackReach(kind),kind==0?8:kind==1?6:12,true);
}

void Game::damageWitness(int amount){
    if(amount<=0 || witnessHealth<=0)return;
    witnessHealth=std::max(0,witnessHealth-amount);
    if(witnessHealth==0){witness.active=false;witness.horror.finish(0);message("god. has fallen to the defenses");}
}

bool Game::strikeWitness(Vec start,double reach,int damage,bool reward){
    if(!combatEnabled || witnessHealth<=0 || !witness.active || !witness.rig.ready || witness.level!=level || witness.family!=family)return false;
    const auto& rig=witness.rig;
    // Check the real posed mesh, not proximity. Terrain and furniture can intercept the strike.
    for(Vec target:{rig.frame.point(rig.pose.chest),rig.frame.point(rig.pose.neck),rig.frame.point(rig.pose.pelvis)}){
        const Vec delta=target-start;const double d=length(delta);if(d<.01 || d>reach+.45)continue;
        const Vec dir=delta/d;
        const auto obstruction=witnessRay(start,dir,reach);
        const double limit=obstruction?obstruction->distance:reach;
        const double hit=witnessHit(start,dir,limit);
        if(hit>=limit)continue;
        const auto cap=std::int64_t(999999)*FullBlock;
        if(reward)bag[PulseRounds]=std::min(cap,bag[PulseRounds]+FullBlock);
        witness.rage=std::min(1.,witness.rage+.21);witness.stain=std::min(1.,witness.stain+.07);
        auto& bolt=bolts[nextBolt++%bolts.size()];bolt={start,start+dir*hit,0,true,witness.scale,family,level};
        damageWitness(damage);
        return true;
    }
    return false;
}

void Game::updateGoblin(double dt){
    const auto* site=modernWorld?settlement::site(level):nullptr;
    if(!site || !wantsCapture())return;
    const Vec player=level<0?feet:innerFeet;
    if(level<0 && world.seed && length(player-guardChest())>80)return;
    const bool present=witness.active && witness.level==level && witness.family==family;
    for(int role=0;role<guardCount();role++)goblin(role);
    if(inFortress()){updateBrood(dt);updateFortressDefenses(dt);}
    bool alarm=false;
    for(int role=0;role<guardCount();role++)alarm|=goblin(role).fallenToWitness;
    const bool firstMealComplete=alarm && !(present && witness.horror.mode==dread::Mode::Feed && witness.horror.prey.kind==dread::Kind::Goblin);
    for(int role=0;role<guardCount();role++){
        auto& guard=goblin(role);
        const int kind=guardClass(role);const bool tower=inFortress() && role<4;
        const double attackReach=tower?13.:settlement::attackReach(kind);
        if(guard.health<=0){if(!guard.fallenToWitness)guard.visualClock=std::min(dread::FallDuration,guard.visualClock+dt);continue;}
        guard.visualClock+=dt;guard.visualMotion*=std::exp(-dt*10);
        guard.attackWait=std::max(0.,guard.attackWait-dt);
        const double playerDistance=length(player-guard.feet);
        if(playerDistance>55)continue;
        const auto sweep=[&](Vec p,int axis,double distance){
            if(level>=0)return layer(family,level).sweep(p,settlement::GuardScale,axis,distance,1.44);
            return world.sweep(p,settlement::GuardScale,axis,distance,1.44);
        };
        // Modern ground is no longer a flat authored floor. Snap a trapped
        // spawn out of a shallow slope and keep gravity authoritative after edits.
        if(level<0 && world.seed && world.sculptedTerrain){
            const auto cell=floorCell(guard.feet);
            if(!world.tileReady(divFloor(cell.x,32),divFloor(cell.z,32)))continue;
            if(world.overlaps(guard.feet,settlement::GuardScale,1.44)){
                for(double dy:{.12,.25,.45,.75,1.1})if(!world.overlaps(guard.feet+Vec{0,dy,0},settlement::GuardScale,1.44)){guard.feet.y+=dy;break;}
            }
        }
        guard.fallSpeed=std::min(10.,guard.fallSpeed+dt*18);const double fall=-guard.fallSpeed*dt;
        const double down=sweep(guard.feet,1,fall);guard.feet.y+=down;if(down>fall+1e-7)guard.fallSpeed=0;
        const bool hunted=present && witness.horror.mode==dread::Mode::Hunt && witness.horror.prey.kind==dread::Kind::Goblin && witness.horror.prey.index==guardId(role);
        const Vec enemy=witness.feet-guard.feet;
        const double range=dread::horizontal(enemy);
        if(hunted && range<=settlement::DefenseRange && std::abs(enemy.y)<2.)guard.defending=true;
        if((firstMealComplete || (inFortress() && range<18)) && present)guard.defending=true;
        const bool fighting=present && witness.active && witnessHealth>0 && guard.defending && combatEnabled;
        const Vec focus=fighting?witness.feet:player;
        const double look=std::atan2(focus.x-guard.feet.x,focus.z-guard.feet.z);
        const auto turn=[&](double wanted,double speed){guard.yaw+=std::clamp(std::remainder(wanted-guard.yaw,2*Pi),-dt*speed,dt*speed);};
        if(!guard.alert && playerDistance<10.5){guard.alert=true;guard.blockTime=0;if(role==0)message("The guardians are returning to their chest");}
        if(guard.attackAge>=0){
            const double before=guard.attackAge;guard.attackAge+=dt/.66;turn(look,12.);
            if(before<.52 && guard.attackAge>=.52 && fighting)goblinStrike(guard,role);
            if(guard.attackAge>=1){guard.attackAge=-1;guard.attackWait=tower?2.4:settlement::attackPeriod(kind);}
            continue;
        }
        if(fighting && range<=attackReach-.25 && std::abs(enemy.y)<(tower?9.:2.5)){
            turn(look,10.);guard.headTurn=0;
            if(guard.attackWait<=0 && std::abs(std::remainder(look-guard.yaw,2*Pi))<.32)guard.attackAge=0;
            continue;
        }
        // A threatened guardian braces rather than running forever at the same speed as the predator.
        if(tower){turn(look,4.);guard.headTurn=std::clamp(std::remainder(look-guard.yaw,2*Pi),-.95,.95);continue;}
        Vec home=inFortress()?settlement::FortressPosts[role]:guardChest()+Vec{role==1?1.45:role==2?-1.45:0.,0,role==1?1.3:role==2?-1.3:0.};
        const Vec heading=fighting?enemy:home-guard.feet;
        const double gap=dread::horizontal(heading),stop=fighting?attackReach-.38:1.0;
        if((!fighting && !guard.alert) || gap<=stop+.1 || guard.fallSpeed>.2){turn(look,4.);guard.headTurn=std::clamp(std::remainder(look-guard.yaw,2*Pi),-.95,.95);continue;}
        const Vec forward=unit(Vec{heading.x,0,heading.z}),side{forward.z,0,-forward.x};
        const double travel=std::min(dt*(fighting?3.2:4.2),gap-stop);
        bool moved=false;
        for(const Vec direction:{forward,unit(forward+(guard.blockTime<.8?side:side*-1)),unit(forward+(guard.blockTime<.8?side*-1:side))}){
            Vec candidate=guard.feet;
            for(int axis:{0,2})candidate[axis]+=sweep(candidate,axis,direction[axis]*travel);
            if(dread::horizontal(candidate-guard.feet)<.9*travel){
                Vec step=guard.feet;const double lift=sweep(step,1,.42);step.y+=lift;
                for(int axis:{0,2})step[axis]+=sweep(step,axis,direction[axis]*travel);
                const double drop=sweep(step,1,-lift-.42);step.y+=drop;
                if(drop>-lift-.419 && dread::horizontal(step-guard.feet)>dread::horizontal(candidate-guard.feet))candidate=step;
            }
            if(dread::horizontal(candidate-guard.feet)<.35*travel)continue;
            const double drop=sweep(candidate,1,-.42);if(drop<=-.419)continue;
            candidate.y+=drop;
            bool occupied=false;for(int other=0;other<guardCount();other++)if(other!=role){const auto& g=goblin(other);if(g.health>0 && length(candidate-g.feet)<.63 && length(candidate-g.feet)<length(guard.feet-g.feet))occupied=true;}
            if(occupied)continue;
            guard.feet=candidate;turn(std::atan2(direction.x,direction.z),9.);
            guard.stride=std::fmod(guard.stride+dt*13.,2*Pi);guard.blockTime=0;guard.visualMotion=1;moved=true;break;
        }
        if(!moved){guard.blockTime+=dt;turn(look,4.);}
        guard.headTurn=std::clamp(std::remainder(look-guard.yaw,2*Pi),-.95,.95);
    }
}

Vec Game::goblinCorpseBase(Vec feet,double heading,int kind,double progress){
    const Vec forward{std::sin(heading),0,std::cos(heading)};
    const Vec right{forward.z,0,-forward.x};
    const double size=settlement::GuardScale;
    const auto slide=forward*(dread::fallSlide(progress)*size);
    // Stop the body slide at a low wall. Probe underneath the skull, trunk
    // and limbs so a slope or a step can actually support the corpse.
    if(const double distance=length(slide);distance>0.001){
        const auto wall=witnessRay(feet+Vec{0,.22*size,0},forward,distance);
        feet+=forward*(wall?std::max(0.,wall->distance-.025*size):distance);
    }
    if(progress<=.22)return feet;
    double support=-.65*size;
    bool found=false;
    for(const Vec contact:fallenGoblinContacts[kind]){
        const Vec spot=feet+(right*contact.x+forward*contact.z+Vec{0,contact.y+.62,0})*size;
        if(const auto floor=witnessRay(spot,{0,-1,0},1.48*size)){
            support=std::max(support,.62*size-floor->distance);
            found=true;
        }
    }
    if(found)feet.y+=support*dread::soft((progress-.22)/.60);
    return feet;
}

void Game::drawGoblinCollapse(const settlement::Guard& guard,Vec feet,double heading,int model,double progress){
    const auto base=goblinCorpseBase(feet,heading,model/2,progress);
    const double size=settlement::GuardScale,angle=dread::fallAngle(progress);
    if(progress>=1.){
        renderer.draw(fallenGoblin[model],base+Vec{0,fallenGoblinFloor[model]*size+.005,0},
                      size,heading,nullptr,0,0,0,false,angle);
        return;
    }
    goblinRigs[model].pose(fallPose,guard.deathPhase,guard.deathMotion,guard.deathIdle,progress,
                          guard.deathLook,guard.deathAttack,-1,guard.deathImpact);
    const double floor=dread::supportHeight(fallPose.points,angle);
    fallingGoblin.upload(fallPose,true);
    renderer.draw(fallingGoblin,base+Vec{0,floor*size+.005,0},size,heading,nullptr,0,0,0,false,angle);
}

void Game::drawGoblin(const View& view){
    const auto* site=modernWorld?settlement::site(level):nullptr;if(!site)return;
    const auto visible=[&](Vec point,double radius,double height){return view.sees({point-Vec{radius,0,radius},point+Vec{radius,height,radius}},double(fieldWidth)/fieldHeight,view.fog);};
    auto& primary=goblin();const Vec chest=guardChest();
    if(visible(chest,.54,.90))renderer.draw(chestModels[primary.looted?1:0],chest);
    for(int role=0;role<guardCount();role++){
        auto& guard=goblin(role);const int kind=guardClass(role);
        if(guard.health<=0){
            // Witness victims are drawn by drawHorror; player kills use the
            // same continuous collapse, then retain the final resting pose.
            if(guard.fallenToWitness || !visible(guard.feet,2.3*settlement::GuardScale,1.72*settlement::GuardScale))continue;
            const double t=std::clamp(guard.visualClock/dread::FallDuration,0.,1.);
            const int lod=length(view.camera-guard.feet)>(renderer.graphics().detail<2?5.5:9.)?1:0;
            drawGoblinCollapse(guard,guard.feet,guard.yaw,kind*2+lod,t);
            continue;
        }
        if(!visible(guard.feet,.9*settlement::GuardScale,1.72*settlement::GuardScale))continue;
        const double range=length(view.camera-guard.feet),limit=renderer.graphics().detail<2?5.5:9.;
        const int lod=range>limit?1:0,i=kind*2+lod;
        const double tick=std::floor(guard.visualClock*(lod?12:24));
        if(inFortress()){
            auto& cache=fortPoses[role];
            if(cache.id!=guardId(role) || cache.lod!=lod || cache.tick!=tick){
                goblinRigs[i].pose(goblinPosed,guard.stride,guard.visualMotion,guard.visualClock,0,guard.headTurn,guard.attackAge);
                cache.mesh.upload(goblinPosed,true);cache.id=guardId(role);cache.lod=lod;cache.tick=tick;
            }
            renderer.draw(cache.mesh,guard.feet,settlement::GuardScale,guard.yaw);continue;
        }
        // Independent upload slots prevent one guard's pose from overwriting another.
        if(goblinPoseSlots[i]!=guardId(role) || goblinPoseClocks[i]!=tick){
            goblinRigs[i].pose(goblinPosed,guard.stride,guard.visualMotion,guard.visualClock,0,guard.headTurn,guard.attackAge);
            goblinModels[i].upload(goblinPosed,true);goblinPoseSlots[i]=guardId(role);goblinPoseClocks[i]=tick;
        }
        renderer.draw(goblinModels[i],guard.feet,settlement::GuardScale,guard.yaw);
    }
}

bool Game::useGoblinChest(){
    const auto* site=modernWorld?settlement::site(level):nullptr;
    if(!site)return false;
    const Vec eye=camera(),ray=viewDirection(yaw,pitch);
    const double size=level<0?body():innerSize();
    const double reach=4.0*std::sqrt(size);
    const Vec chest=guardChest();
    const double hit=rayBox(eye,ray,chest-Vec{0.42,0,0.30},chest+Vec{0.42,0.62,0.30},reach);
    if(hit>=reach || (target && target->distance<hit))return false;
    auto& guard=goblin();
    bool guarded=false;for(int role=0;role<guardCount();role++){auto& g=goblin(role);guarded|=g.health>0 && (inFortress() || length(g.feet-chest)<2.1);}
    if(guard.looted)message("The goblin chest is empty");
    else if(guarded){
        message("The goblin is guarding its chest");
    }else{
        guard.looted=true;
        bag[PulseRounds]=std::min(std::int64_t(999999)*FullBlock,bag[PulseRounds]+4*FullBlock);
        awardLoot(6);
        message("Chest looted. Four rounds. "+status);
    }
    return true;
}

int Game::goblinTarget(Vec eye,Vec ray,double reach,double& hit) const{
    hit=reach;int chosen=-1;
    if(!modernWorld || !settlement::site(level))return chosen;
    for(int role=0;role<guardCount();role++){
        const int id=guardId(role);const auto& guard=guards[id];if(!guard.ready || guard.health<=0)continue;
        const double at=rayBox(eye,ray,guard.feet-Vec{.23,0,.20}*settlement::GuardScale,guard.feet+Vec{.23,1.44,.32}*settlement::GuardScale,hit);
        if(at<hit){hit=at;chosen=id;}
    }
    return chosen;
}
double Game::goblinHit(Vec eye,Vec ray,double reach) const{double hit;goblinTarget(eye,ray,reach,hit);return hit;}

void Game::updateBrood(double dt){
    auto& b=broods[family];b.clock+=dt;
    if(b.slot<0){
        for(int i=0;i<guardCount();i++){
            const auto& g=goblin(i);
            if(g.health>0 || (!g.fallenToWitness && g.visualClock<dread::FallDuration))continue;
            if(witness.active && witness.family==family && witness.level==level && witness.horror.mode==dread::Mode::Feed &&
               witness.horror.prey.kind==dread::Kind::Goblin && witness.horror.prey.index==guardId(i))continue;
            b.slot=i;b.age=0;break;
        }
        return;
    }
    if(goblin(b.slot).health>0){b.slot=-1;b.age=0;return;}
    b.age=std::min(settlement::BroodDuration,b.age+dt);
    if(b.age<settlement::BroodDuration)return;
    auto& room=layer(family,level);
    const Vec p=b.slot<4?settlement::FortressPosts[b.slot]:settlement::BroodExit;
    if(room.overlaps(p,settlement::GuardScale,1.44) || length(p-innerFeet)<.85)return;
    for(int i=0;i<guardCount();i++)if(i!=b.slot && goblin(i).health>0 && length(p-goblin(i).feet)<.9)return;
    const bool looted=goblin(b.slot).looted;
    auto& g=goblin(b.slot);g={};g.ready=true;g.alert=true;g.feet=p;g.looted=looted;
    g.yaw=std::atan2(24.5-p.x,28.-p.z);g.attackWait=1.2;
    fortPoses[b.slot].id=-1;
    b.slot=-1;b.age=0;message("A new guardian has grown in the brood hut");
}
void Game::updateFortressDefenses(double dt){
    auto& b=broods[family];
    for(int i=0;i<2;i++){
        auto& wait=b.defenseWait[i];wait=std::max(0.,wait-dt);
        if(wait>0 || !witness.active || witnessHealth<=0 || witness.family!=family || witness.level!=level)continue;
        const Vec p=settlement::Defenses[i];
        if(layer(family,level).at(floorCell(p-Vec{0,.6,0}))==Air)continue;
        if(length(witness.feet-p)>13)continue;
        wait=strikeWitness(p+Vec{0,.28,0},13.,10,false)?3.2:.45;
    }
}
void Game::drawFortress(const View& view){
    if(!fortressArtReady){
        Geometry architecture,base,flame;settlement::fortressArt(architecture,base,flame);
        fortressArchitecture.upload(architecture);fortressFireBase.upload(base);fortressFlame.upload(flame);
        fortressArtReady=true;
    }
    renderer.draw(fortressArchitecture);
    const auto& b=broods[family];
    for(int i=0;i<2;i++){
        const Vec p=settlement::Campfires[i];
        if(!view.sees({p-Vec{1,0,1},p+Vec{1,2,1}},double(fieldWidth)/fieldHeight,view.fog))continue;
        renderer.draw(fortressFireBase,p);
        for(int j=0;j<2;j++)renderer.draw(fortressFlame,p,.88+.09*std::sin(b.clock*(4+j)+i*2+j),b.clock*.6+j*1.8,nullptr,1.0f);
    }
    if(b.slot<0 || !view.sees({settlement::BroodCradle-Vec{1,0,1},settlement::BroodCradle+Vec{1,3,1}},double(fieldWidth)/fieldHeight,view.fog))return;
    const int kind=settlement::fortressClass(b.slot);
    for(int i=0;i<6;i++){
        const double progress=std::clamp(b.age/(settlement::BroodDuration/6)-i,0.,1.);
        if(progress<=0)continue;
        const int part=settlement::GrowthOrder[i];auto& mesh=broodParts[kind][part];
        if(!mesh.vertices){Geometry geo;goblinRigs[kind*2].pose(geo,0,0,0,0,0,-1,part);mesh.upload(geo);}
        const double grown=.12+.88*dread::soft(progress);
        const std::array<Vec,6> pivots={{{0,1.105,0},{0,.66,0},{-.14,1.02,0},{.14,1.02,0},{-.081,.65,0},{.081,.65,0}}};
        const Vec pivot=pivots[part];
        renderer.draw(mesh,settlement::BroodCradle+Vec{-pivot.x,pivot.y,-pivot.z}*(settlement::GuardScale*(1-grown)),settlement::GuardScale*grown,Pi,nullptr,.05f);
    }
}
