#include "contact-rig.hpp"

namespace contact {
namespace {
constexpr Vec up{0,1,0};
double ease(double speed,double dt){return -std::expm1(-speed*dt);}
double mix(double a,double b,double t){return a+(b-a)*t;}
Vec mix(Vec a,Vec b,double t){return a+(b-a)*t;}
Vec limited(Vec p,double limit){const double n=length(p);return n>limit?p*(limit/n):p;}
double span(Bones b,double angle){
    return std::sqrt(b.upper*b.upper+b.lower*b.lower-2*b.upper*b.lower*std::cos(angle));
}
Vec tangent(Vec axis,Vec preference){
    Vec result=preference-axis*dot(axis,preference);
    if(length(result)<1e-6){
        Vec fallback=std::abs(axis.y)<0.9?up:Vec{1,0,0};
        result=fallback-axis*dot(axis,fallback);
    }
    return unit(result);
}

struct Goal {
    const Frame& frame;
    const Queries& scene;
    Vec root,home,travel,wall;
    bool arm,climbing,crawl;
    double side,pad;
    Bones bones;

    std::optional<Surface> cast(Vec from,Vec dir,double distance) const {
        return scene.cast(frame.point(from),frame.vector(unit(dir)),distance*frame.scale);
    }
    bool reachable(const Surface& surface) const {
        if(dot(surface.normal,surface.normal)<0.5)return false;
        const Vec target=frame.local(surface.point+surface.normal*(pad*frame.scale));
        const double d=length(target-root);
        if(d>span(bones,bones.maximumAngle)-0.025 || d<span(bones,bones.minimumAngle)+0.01)return false;
        // A palm can round the top of a ledge. Reject a candidate hidden entirely
        // behind the obstacle by checking the endpoint from a short outward offset.
        const Vec normal=frame.localVector(surface.normal);
        const Vec approach=target+normal*0.18;
        auto blocker=cast(approach,normal*-1,0.18);
        return !blocker;
    }
    std::optional<Surface> find() const {
        if((arm && !crawl) || climbing){
            const Vec outward=climbing?wall:unit(Vec{side*0.35,0,1});
            for(Vec dir : {outward,Vec{side,0,0}}){
                const double height=climbing?(arm?root.y+0.42:root.y-0.40):root.y-0.46;
                Vec start{home.x,height,climbing?0.07:root.z};
                start.y+=travel.y;
                auto face=cast(start,dir,arm?1.96:1.34);
                if(!face || std::abs(face->normal.y)>0.73){
                    // Once the shoulder passes the lip, a horizontal ray goes
                    // over it. Reach down over the edge to finish the climb.
                    if(climbing){
                        Vec above=start+dir*1.30+up*0.32;
                        auto lip=cast(above,up*-1,arm?2.45:1.25);
                        if(lip && lip->normal.y>0.52 && frame.local(lip->point).y>0.05 &&
                            reachable(*lip))return lip;
                    }
                    continue;
                }
                // Search down over the lip first. Tall uninterrupted walls give
                // a palm brace, while ledges give a top surface to hook onto.
                if(arm){
                    Vec rim=frame.local(face->point)+dir*0.09+up*0.56;
                    auto ledge=cast(rim,up*-1,0.68);
                    if(ledge && ledge->normal.y>0.52 && reachable(*ledge))return ledge;
                }
                if(reachable(*face))return face;
            }
        }
        if(climbing || (arm && !crawl))return {};
        Vec center=home+travel;
        for(Vec offset : {Vec{},Vec{side*0.14,0,-0.19},Vec{-side*0.18,0,0.20},travel*-1}){
            Vec above=center+offset+up*0.85;
            auto ground=cast(above,up*-1,1.72);
            if(ground && ground->normal.y>0.46 && reachable(*ground))return ground;
        }
        return {};
    }
};
}

Vec Frame::vector(Vec v) const {
    double c=std::cos(heading),s=std::sin(heading);
    return {v.x*c+v.z*s,v.y,-v.x*s+v.z*c};
}
Vec Frame::localVector(Vec v) const {
    double c=std::cos(heading),s=std::sin(heading);
    return {v.x*c-v.z*s,v.y,v.x*s+v.z*c};
}
Vec Frame::point(Vec p) const {return origin+vector(p)*scale;}
Vec Frame::local(Vec p) const {return localVector((p-origin)/scale);}

namespace {
// Closest points on two finite segments (including parallel and degenerate
// segments). This is bounded work and doesn't depend on the visual LOD.
double segmentDistance(Vec p,Vec q,Vec a,Vec b){
    const Vec d=q-p,e=b-a,r=p-a;
    const double dd=dot(d,d),ee=dot(e,e),de=dot(d,e),dr=dot(d,r),er=dot(e,r);
    double s=0,t=0;
    if(dd<1e-12 && ee<1e-12)return length(r);
    if(dd<1e-12)t=std::clamp(er/ee,0.0,1.0);
    else if(ee<1e-12)s=std::clamp(-dr/dd,0.0,1.0);
    else{
        const double denom=dd*ee-de*de;
        if(denom>1e-12)s=std::clamp((de*er-ee*dr)/denom,0.0,1.0);
        t=(de*s+er)/ee;
        if(t<0){t=0;s=std::clamp(-dr/dd,0.0,1.0);}
        else if(t>1){t=1;s=std::clamp((de-dr)/dd,0.0,1.0);}
    }
    return length(r+d*s-e*t);
}
struct Capsule{Vec a,b;double radius;};
std::array<Capsule,3> envelopes(const Chain& c,bool arm){
    // Match livingLimb's largest radii conservatively. The narrow terminal
    // capsule covers palm/fingers or foot, including the contact tangent.
    const Vec forward=length(c.forward)>.5?unit(c.forward):Vec{0,0,1};
    return {{{c.root+(c.joint-c.root)*.12,c.joint,arm?.125:.175},
        {c.joint,c.tip,arm?.090:.112},
        {c.tip-forward*.08,c.tip+forward*(arm?.32:.21),arm?.095:.082}}};
}
}
double limbClearance(const Chain& a,bool armA,const Chain& b,bool armB){
    double result=1e9;
    for(const auto& i:envelopes(a,armA))for(const auto& j:envelopes(b,armB))
        result=std::min(result,segmentDistance(i.a,i.b,j.a,j.b)-i.radius-j.radius);
    return result;
}

Chain solveChain(Vec root,Vec target,Vec pole,Bones b){
    const Vec delta=target-root;
    const double distance=length(delta);
    const Vec axis=distance>1e-8?delta/distance:Vec{0,-1,0};
    const double reach=std::clamp(distance,span(b,b.minimumAngle),span(b,b.maximumAngle));
    const double along=(b.upper*b.upper-b.lower*b.lower+reach*reach)/(2*reach);
    const double height=std::sqrt(std::max(0.0,b.upper*b.upper-along*along));
    return {root,root+axis*along+tangent(axis,pole)*height,root+axis*reach};
}

void Rig::update(const Frame& next,bool low,bool climbing,bool falling,Vec climbDirection,
    double headroom,double dt,const Queries& scene){
    dt=std::clamp(dt,0.0,0.12);
    const bool fresh=!ready || length(next.origin-frame.origin)>2.6*next.scale ||
        next.scale/frame.scale>1.28 || next.scale/frame.scale<0.78;
    if(fresh){
        limbs={};velocity={};compensation={};peekBlend={};roll=0;idleClock=0;idleWeight=0;feedBlend=feedWeight=0;
        crouch=low?1:0;climb=climbing?1:0;
    }else if(dt>1e-6){
        velocity=mix(velocity,limited((next.origin-frame.origin)/(next.scale*dt),16),ease(18,dt));
    }
    frame=next;
    ready=true;
    // Sink into the crawl quickly, but unfold into upright posture slowly.
    const bool crouchRequested=low || climbing;
    const double previousCrouch=crouch;
    crouch=mix(crouch,crouchRequested?1:0,ease(crouchRequested?12.0:2.7,dt));
    climb=mix(climb,climbing?1:0,ease(10,dt));
    // Drop immediately below a newly encountered ceiling, but rise at a limited rate.
    const double standingHeight=anatomy.headHeight+0.44;
    crouch=std::max(crouch,std::clamp((standingHeight-headroom)/(standingHeight-2.17),0.0,1.0));
    const double h=crouch,wallBlend=climb;
    feedBlend=mix(feedBlend,feeding && !climbing && !falling?1.0:0.0,ease(10,dt));
    feedWeight=mix(feedWeight,feedingEffort*feedBlend,ease(12,dt));
    idleClock+=dt;
    idleWeight=mix(idleWeight,(!climbing && !falling && length(velocity)<.24)?1.0:0.0,ease(4,dt));
    // Slow asymmetric breathing interrupted by a short, held muscle spasm.
    // Keep this in the IK targets so planted fingers/feet don't slide.
    const double breath=std::sin(idleClock*1.12+std::sin(idleClock*.24)*.9);
    const double catchPose=std::pow(.5+.5*std::sin(idleClock*.81),26.0);
    const double lean=idleWeight*(.025*std::sin(idleClock*.43)+.018*catchPose);
    const Vec stride=limited(frame.localVector(velocity)*0.145,0.88);
    Vec wall=unit(frame.localVector({climbDirection.x,0,climbDirection.z}));
    if(length(wall)<0.1)wall={0,0,1};
    // A low hip and forward-reaching shoulders make the crawl predatory;
    // real contact points still come from IK queries against world geometry.
    const Vec pelvis{0,mix(anatomy.hipHeight,0.84,h),mix(-0.09,-0.68,h)+wallBlend*0.44};
    const Vec chest{-0.045,mix(anatomy.chestHeight,1.44,h)+wallBlend*0.45,
        mix(0.21,0.62,h)-wallBlend*0.39};
    Vec bodyOffset{};
    double supportY=0,bank=0;
    int footCount=0;
    for(int i=0;i<2;++i){
        const auto& foot=limbs[i];
        if(foot.phase!=Phase::Stance)continue;
        const Vec position=frame.local(foot.tip);
        if(foot.normal.y>0.45){
            supportY+=position.y-anatomy.anklePad;
            bank+=std::clamp(position.y-anatomy.anklePad,-0.32,0.32)*(i?1:-1);
        }
        // Shift mass towards the supporting foot before the other foot lands.
        bodyOffset.x+=std::clamp(position.x,-0.55,0.55)*0.19;
        bodyOffset.z+=std::clamp(position.z-pelvis.z,-0.7,0.7)*0.08;
        ++footCount;
    }
    if(footCount){
        bodyOffset=bodyOffset/double(footCount);
        bodyOffset.y=std::clamp(supportY/footCount,-0.33,0.28);
    }
    bodyOffset.y=std::min(bodyOffset.y,std::max(0.0,headroom-mix(standingHeight,2.17,h)));
    compensation=mix(compensation,bodyOffset,ease(12,dt));
    roll=mix(roll,std::clamp(bank*0.24,-0.12,0.12),ease(12,dt));
    pose.pelvis=pelvis+compensation+Vec{lean*.35,0,0};
    Vec brace{};
    for(int i=2;i<4;++i)if(limbs[i].phase==Phase::Stance && limbs[i].normal.y<0.5)
        brace+=frame.localVector(limbs[i].normal)*0.07;
    pose.chest=chest+compensation*0.63+brace+Vec{lean,idleWeight*(.014*breath-.012*catchPose),idleWeight*.016*breath};
    peekBlend=mix(peekBlend,frame.localVector(peekOffset),ease(4,dt));
    pose.chest+=peekBlend*.27;
    pose.neck=pose.chest+peekBlend*.73+Vec{-0.035,mix(anatomy.headHeight-anatomy.chestHeight-.23,0.255,h),
        mix(0.22,0.44,h)-wallBlend*0.20};
    pose.neck+=Vec{idleWeight*(.023*std::sin(idleClock*.37)-.025*catchPose),idleWeight*.012*breath,0};

    // Transfer weight to the planted hand and bow the spine during a bite.
    // The limb solver below still owns every contact point.
    pose.pelvis+=Vec{-.07,-.04,.04}*feedWeight;
    pose.chest+=Vec{-.12,-.08,.06}*feedWeight;
    pose.neck+=Vec{-.08,-.08,.10}*feedWeight;

    std::array<Chain,4> solved{};
    for(int i=0;i<4;++i){
        auto& limb=limbs[i];
        const bool arm=i>=2;
        const double side=(i%2)?1:-1;
        const Bones bones=arm?anatomy.arm:anatomy.leg;
        const double pad=arm?anatomy.palmPad:anatomy.anklePad;
        const double shoulderDraw=arm?h*(.24+.16*std::clamp(frame.localVector(velocity).z*.20,-1.0,1.0)):0;
        const Vec root=(arm?pose.chest:pose.pelvis)+Vec{side*(arm?anatomy.shoulderWidth:anatomy.hipWidth),
            (arm?-.09+.11*h:0)+side*roll*(arm?0.55:1),arm?-.03-shoulderDraw:0};
        const Vec home{side*(arm?mix(.57,.68,h):.35),0,(arm?mix(0.28,1.53,h):mix(-0.035,-0.89,h))};
        const Vec freeTip=arm?root+Vec{side*0.16,-mix(2.40,1.02,h),mix(.18,1.08,h)}:
            root+Vec{side*0.12,-mix(1.81,0.69,h),-0.14};
        if(fresh)limb.tip=frame.point(freeTip);
        const bool usingHand=i==3 && feedBlend>.015;
        // A released feeding hand must not acquire and drop ground contacts
        // every frame. Its target is authored in world space by the timeline.
        if(usingHand){limb.phase=Phase::Free;limb.retry=.12;}
        limb.retry=std::max(0.0,limb.retry-dt);

        if(limb.phase!=Phase::Free){
            bool valid=!falling;
            if(valid && limb.support.attachment)valid=scene.follow && scene.follow(limb.support);
            if(valid){
                const Vec n=limb.support.normal;
                const double slack=0.085*frame.scale;
                auto under=scene.cast(limb.support.point+n*slack,n*-1,2*slack);
                // Confirm the same face still exists. Small normal-direction motion
                // is followed; removal or a large normal change releases immediately.
                valid=under && dot(under->normal,n)>0.65 &&
                    length(under->point-limb.support.point)<slack*0.97;
                if(valid)limb.support=*under;
            }
            if(!valid){limb.phase=Phase::Free;limb.retry=0;}
        }
        const Vec end=limb.support.point+limb.support.normal*(pad*frame.scale);
        const double reach=length(frame.local(end)-root);
        const double bias=arm?mix(1.28,.64,h):mix(0.62,0.51,h);
        const double error=climbing?std::abs(frame.local(end).y-(root.y+(arm?0.42:-0.40))):
            length(Vec{frame.local(end).x-home.x,0,frame.local(end).z-home.z});
        const bool stance=limb.phase==Phase::Stance;
        const bool emergency=stance && reach>span(bones,bones.maximumAngle)-0.022;
        const bool mateSwing=limbs[i^1].phase==Phase::Swing;
        const bool stale=stance && (emergency || (!mateSwing && error>bias));
        if((stale || limb.phase==Phase::Free) && !falling && !usingHand && (limb.retry==0 || emergency)){
            Goal goal{frame,scene,root,home,stride,wall,arm,climbing,h>0.62,side,pad,bones};
            auto support=goal.find();
            if(support && (!stance || length(support->point-limb.support.point)>0.18*frame.scale)){
                limb.start=limb.tip;
                limb.support=*support;
                limb.forward=tangent(support->normal,
                    std::abs(support->normal.y)<0.6?up:frame.vector({0,0,1}));
                limb.progress=0;
                limb.duration=std::clamp((arm?0.28:0.22)/(1+0.10*length(velocity)),0.09,0.30);
                limb.phase=fresh?Phase::Stance:Phase::Swing;
            }else if(emergency){
                limb.phase=Phase::Free;
            }
            limb.retry=(arm && h<=.62 && !climbing)?.22:.09;
        }
        if(limb.phase!=Phase::Free){
            Vec target=limb.support.point+limb.support.normal*(pad*frame.scale);
            if(limb.phase==Phase::Swing){
                limb.progress=std::min(1.0,limb.progress+dt/limb.duration);
                const double t=limb.progress, smooth=t*t*t*(10+t*(-15+6*t));
                const double altitude=(arm?0.18:0.22)+
                    std::min(0.36,std::abs(target.y-limb.start.y)/frame.scale*0.65);
                const Vec lift=unit(limb.support.normal+up*0.45);
                limb.tip=mix(limb.start,target,smooth)+lift*(16*t*t*(1-t)*(1-t)*altitude*frame.scale);
                if(t>=1)limb.phase=Phase::Stance;
            }else limb.tip=target;
            limb.normal=limb.support.normal;
            limb.forward=tangent(limb.normal,limb.forward);
        }else{
            // Retain a collision-resolved resting offset instead of repeatedly
            // pulling a free limb into the same blocked location.
            if(length(velocity)>.25 || climbing || falling || std::abs(h-limb.restCrouch)>.12)
                limb.restOffset=mix(limb.restOffset,Vec{},ease(3,dt));
            if(!usingHand)limb.tip=mix(limb.tip,frame.point(freeTip+limb.restOffset),fresh?1:ease(9,dt));
            limb.normal=up;limb.forward=frame.vector({0,0,1});
        }

        // Release only the feeding hand. The ordinary terrain and limb
        // clearance solver below still approves or rejects its requested pose.
        if(usingHand){
            limb.phase=Phase::Free;
            limb.retry=.08;
            limb.tip=mix(limb.tip,feedingTarget,feedBlend*ease(16,dt));
            limb.normal=up;limb.forward=frame.vector({0,0,1});
        }
        const Vec tip=frame.local(limb.tip);
        // Keep elbows trailing and knees ahead of the body. Test both bones, and
        // rotate the bend plane around the anchored tip when an obstacle blocks it.
        const Vec pole=arm?mix(Vec{side*.42,.10,-1},Vec{side*.36,.35,-.85},h):mix(Vec{side*0.22,0.06,1},Vec{side*0.88,0.05,-0.52},wallBlend);
        const Vec axis=unit(tip-root);
        const Vec preferred=tangent(axis,pole);
        const auto clear=[&](Vec a,Vec b){
            const double len=length(b-a);
            if(len<0.025)return true;
            return !scene.cast(frame.point(a),frame.vector(unit(b-a)),(len-0.018)*frame.scale);
        };
        Vec bend=preferred;
        if(!fresh && length(limb.bend)>0.1)
            bend=tangent(axis,frame.localVector(limb.bend));
        // Prefer the existing bend plane. Only relax toward the anatomical
        // pole when doing so is safe; this prevents solver branch flipping.
        if(length(bend)<.1)bend=preferred;
        const auto oriented=[&](Chain c){
            c.normal=frame.localVector(limb.normal);c.forward=frame.localVector(limb.forward);c.pad=pad;return c;
        };
        const auto separation=[&](const Chain& c){
            double distance=1e9;
            for(int j=0;j<i;++j)distance=std::min(distance,limbClearance(c,arm,solved[j],j>=2));
            // Do not test the attachment itself: the shoulder/hip is supposed
            // to join the trunk. The distal upper bone and all of the lower
            // bone must stay outside the torso's tapered envelope.
            const Vec trunkA=pose.pelvis+(pose.chest-pose.pelvis)*.14;
            const Vec trunkB=pose.pelvis+(pose.chest-pose.pelvis)*.87;
            distance=std::min(distance,segmentDistance(c.root+(c.joint-c.root)*.48,c.joint,trunkA,trunkB)-(arm?.30:.28));
            distance=std::min(distance,segmentDistance(c.joint,c.tip,trunkA,trunkB)-(arm?.32:.30));
            // The skull and neck are also solid exclusions, except for the
            // shoulder attachment. Bone/skin seams may join by design.
            const Vec head=pose.neck;
            for(const auto& cap:envelopes(c,arm)){
                distance=std::min(distance,segmentDistance(cap.a,cap.b,head,head)-(.30+cap.radius));
            }
            return distance;
        };
        const auto terrainClear=[&](const Chain& c){
            if(!clear(c.root,c.joint) || !clear(c.joint,c.tip))return false;
            // Sample the outer skin as well as the skeletal centerline. The
            // radii follow the same taper used by livingLimb. Narrow the
            // terminal section at the contact pad so it can plant on slopes.
            const auto shell=[&](Vec a,Vec b,double ra,double rb){
                const Vec axis=unit(b-a);
                if(length(axis)<.5)return true;
                const Vec sideAxis=tangent(axis,Vec{1,0,0});
                const Vec vertical=unit(cross(axis,sideAxis));
                for(Vec direction:{sideAxis,sideAxis*-1,vertical,vertical*-1})
                    if(!clear(a+direction*ra,b+direction*rb))return false;
                return true;
            };
            return shell(c.root,c.joint,arm?.095:.125,arm?.060:.075) &&
                   shell(c.joint,c.tip,arm?.060:.075,arm?.028:.035);
        };
        auto joint=oriented(solveChain(root,tip,bend,bones));
        bool unobstructed=terrainClear(joint);
        double best=unobstructed?separation(joint):-1e9;
        if(!fresh && unobstructed && best>.018 && dot(bend,preferred)<.998 &&
            (length(velocity)>.25 || climbing || std::abs(h-previousCrouch)>.001)){
            const Vec relaxed=tangent(axis,mix(bend,preferred,ease(3,dt)));
            auto trial=oriented(solveChain(root,tip,relaxed,bones));
            const double score=separation(trial);
            if(score>.018 && terrainClear(trial)){joint=trial;bend=relaxed;best=score;}
        }
        // First rotate the elbow/knee about a planted endpoint. Preserve the
        // contact whenever possible; unplant only when no clear pose exists.
        if(!unobstructed || best<0){
            for(double angle:{.26,-.26,.52,-.52,1.1,-1.1,1.8,-1.8,Pi}){
                const Vec alternative=bend*std::cos(angle)+cross(axis,bend)*std::sin(angle);
                auto trial=oriented(solveChain(root,tip,alternative,bones));
                const double score=separation(trial);
                if(score<=best || !terrainClear(trial))continue;
                joint=trial;bend=alternative;best=score;unobstructed=true;
                if(best>=.012)break;
            }
        }
        if(!unobstructed || best<0){
            // A planted endpoint can be impossible in a narrow gap. Release
            // it, and try bounded outward/lifted targets rather than stretching
            // bones through another limb. All accepted chains keep bone lengths.
            const Vec outward{side,0,0};
            for(double shift:{.18,.36,.60,.90})for(double lift:{.10,.28,.48}){
                const Vec target=tip+outward*shift+up*lift;
                const Vec alternative=preferred+outward*.45;
                auto trial=oriented(solveChain(root,target,alternative,bones));
                const double score=separation(trial);
                if(score<=best || !terrainClear(trial))continue;
                joint=trial;bend=alternative;best=score;unobstructed=true;
                if(best>=.012)break;
            }
            limb.phase=Phase::Free;limb.retry=.18;
            limb.tip=frame.point(joint.tip);
            if(!usingHand && unobstructed && best>=0){limb.restOffset=joint.tip-freeTip;limb.restCrouch=h;}
        }
        unobstructed=unobstructed && best>=0;
        limb.bend=frame.vector(bend);
        joint.normal=frame.localVector(limb.normal);
        joint.forward=frame.localVector(limb.forward);
        joint.pad=pad;
        joint.grip=usingHand?feedingGrip*feedBlend:0.;
        joint.planted=unobstructed && limb.phase==Phase::Stance && length(joint.tip-tip)<0.018;
        if(!joint.planted && limb.phase==Phase::Stance){limb.phase=Phase::Free;limb.retry=0;}
        solved[i]=joint;
        (arm?pose.arms:pose.legs)[i%2]=joint;
    }
}
}
