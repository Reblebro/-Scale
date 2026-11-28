#include "render.hpp"
#include "platform.hpp"
#include "world.hpp"
#include "natural-world.hpp"
#include <array>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <unordered_map>

namespace {
using U=unsigned;
using I=int;
using F=float;
using B=unsigned char;
using P=std::ptrdiff_t;
using S=void*;
using U64=std::uint64_t;
#define GL_API(X) \
    X(void,Viewport,I,I,I,I) X(void,ClearColor,F,F,F,F) X(void,Clear,U) X(void,Enable,U) \
    X(void,Disable,U) X(void,DepthFunc,U) X(void,DepthMask,B) X(void,BlendFunc,U,U) X(U,CreateShader,U) \
    X(void,ShaderSource,U,I,const char* const*,const I*) X(void,CompileShader,U) \
    X(void,GetShaderiv,U,U,I*) X(void,GetShaderInfoLog,U,I,I*,char*) X(void,DeleteShader,U) \
    X(U,CreateProgram) X(void,AttachShader,U,U) X(void,LinkProgram,U) X(void,GetProgramiv,U,U,I*) \
    X(void,GetProgramInfoLog,U,I,I*,char*) X(void,DeleteProgram,U) X(void,UseProgram,U) \
    X(void,GenVertexArrays,I,U*) X(void,BindVertexArray,U) X(void,DeleteVertexArrays,I,const U*) \
    X(void,GenBuffers,I,U*) X(void,BindBuffer,U,U) X(void,BufferData,U,P,const void*,U) \
    X(void,BufferSubData,U,P,P,const void*) X(void,DeleteBuffers,I,const U*) X(void,EnableVertexAttribArray,U) \
    X(void,VertexAttribPointer,U,I,U,B,I,const void*) X(void,DrawArrays,U,I,I) X(void,DrawElements,U,I,U,const void*) \
    X(I,GetUniformLocation,U,const char*) X(void,Uniform1i,I,I) X(void,Uniform1f,I,F) \
    X(void,Uniform2f,I,F,F) X(void,Uniform3f,I,F,F,F) X(void,Uniform4fv,I,I,const F*) \
    X(void,ActiveTexture,U) X(void,GenTextures,I,U*) X(void,BindTexture,U,U) \
    X(void,GetIntegerv,U,I*) X(void,GetFloatv,U,F*) X(const B*,GetStringi,U,U) X(void,TexParameterf,U,U,F) X(void,TexParameteri,U,U,I) X(void,PixelStorei,U,I) X(void,GenerateMipmap,U) \
    X(void,TexImage2D,U,I,I,I,I,I,U,U,const void*) X(void,TexImage3D,U,I,I,I,I,I,I,U,U,const void*) \
    X(void,DeleteTextures,I,const U*) X(void,GenFramebuffers,I,U*) X(void,BindFramebuffer,U,U) \
    X(void,DeleteFramebuffers,I,const U*) X(U,CheckFramebufferStatus,U) \
    X(void,FramebufferTexture2D,U,U,U,U,I) X(void,DrawBuffer,U) X(void,ReadBuffer,U) \
    X(void,GenRenderbuffers,I,U*) X(void,BindRenderbuffer,U,U) X(void,DeleteRenderbuffers,I,const U*) \
    X(void,RenderbufferStorageMultisample,U,I,U,I,I) X(void,RenderbufferStorage,U,U,I,I) X(void,FramebufferRenderbuffer,U,U,U,U) \
    X(void,BlitFramebuffer,I,I,I,I,I,I,I,I,U,U) X(void,ReadPixels,I,I,I,I,U,U,void*) \
    X(S,FenceSync,U,U) X(U,ClientWaitSync,S,U,U64) X(void,DeleteSync,S) X(void,Finish)
#define DECL(R,N,...) R(*gl##N)(__VA_ARGS__)=nullptr;
GL_API(DECL)
#undef DECL
void loadGL() {
#define LOAD(R,N,...) gl##N=reinterpret_cast<decltype(gl##N)>(SDL_GL_GetProcAddress("gl" #N)); if(!gl##N) throw std::runtime_error("Missing graphics function gl" #N);
    GL_API(LOAD)
#undef LOAD
}
constexpr U Framebuffer=0x8d40,ReadFramebuffer=0x8ca8,DrawFramebuffer=0x8ca9,
    Renderbuffer=0x8d41,ColorAttachment=0x8ce0,DepthAttachment=0x8d00,Complete=0x8cd5;
constexpr U ArrayBuffer=0x8892, Static=0x88e4, Stream=0x88e0, Float=0x1406, Byte=0x1401,
    Texture=0x0de1, Layers=0x8c1a, Triangles=4, Depth=0x0b71, Blend=0x0be2, CullFace=0x0b44,
    RGB=0x1907, Red=0x1903, FilterMin=0x2801, FilterMag=0x2800, Linear=0x2601, Edge=0x812f,
    WrapS=0x2802,WrapT=0x2803;
void attribute(U slot,I size,I stride,std::size_t offset) {
    glEnableVertexAttribArray(slot);
    glVertexAttribPointer(slot,size,Float,0,stride,reinterpret_cast<const void*>(offset));
}
U compile(U type,const char* source) {
    U handle=glCreateShader(type);
    glShaderSource(handle,1,&source,nullptr);
    glCompileShader(handle);
    I okay;
    glGetShaderiv(handle,0x8b81,&okay);
    if (!okay) {
        char log[2048]{};
        glGetShaderInfoLog(handle,sizeof(log),nullptr,log);
        glDeleteShader(handle);
        throw std::runtime_error(std::string("Graphics shader: ")+log);
    }
    return handle;
}
U program(const char* vertex,const char* fragment) {
    const U a=compile(0x8b31,vertex),b=compile(0x8b30,fragment);
    const U result=glCreateProgram();
    glAttachShader(result,a); glAttachShader(result,b); glLinkProgram(result);
    glDeleteShader(a);glDeleteShader(b);
    I okay;
    glGetProgramiv(result,0x8b82,&okay);
    if(!okay) {
        char log[2048]{};
        glGetProgramInfoLog(result,sizeof(log),nullptr,log);
        glDeleteProgram(result);
        throw std::runtime_error(std::string("Graphics link: ")+log);
    }
    return result;
}
std::unordered_map<U,std::unordered_map<std::string_view,I>> uniformCache;
I uni(U program,const char* name) {
    auto& known=uniformCache[program];
    auto found=known.find(name);
    if(found!=known.end())return found->second;
    return known.emplace(name,glGetUniformLocation(program,name)).first->second;
}
void uvec(U program,const char* name,Vec value) { glUniform3f(uni(program,name),value.x,value.y,value.z); }

// A small depth-only atlas for the nearby forest. Direction matches the sky.


Vec shadowRight(Vec sun){
    return unit(cross(std::abs(sun.y)>.98?Vec{0,0,1}:Vec{0,1,0},sun));
}
constexpr const char* canopyVertex=R"GLSL(#version 330 core
layout(location=0) in vec3 position;
uniform vec3 offset,sunDir,sunRight,sunUp;
uniform float shadowRadius,shadowDepthRange,modelScale;
uniform vec2 modelYaw,modelPitch;
void main(){
    mat3 rot=mat3(modelYaw.x,0,-modelYaw.y,0,1,0,modelYaw.y,0,modelYaw.x);
    mat3 inc=mat3(1,0,0,0,modelPitch.x,-modelPitch.y,0,modelPitch.y,modelPitch.x);
    vec3 p=rot*inc*position*modelScale+offset;
    gl_Position=vec4(dot(p,sunRight)/shadowRadius,dot(p,sunUp)/shadowRadius,-dot(p,sunDir)/shadowDepthRange,1.0);
}
)GLSL";
constexpr const char* canopyFragment=R"GLSL(#version 330 core
void main(){}
)GLSL";
// Shared by opaque and sky shaders. Camera-relative coordinates retain
// precision and make the same light volume continuous across chunk seams.
constexpr const char* canopyLighting=R"GLSL(
uniform bool canopyActive;
uniform sampler2DShadow canopyMap;
uniform sampler2DShadow creatureMap;
uniform bool creatureActive;
uniform vec3 creatureOffset;
uniform float creatureRadius,creatureDepthRange;
uniform vec3 shadowOffset,sunDir,sunRight,sunUp;
uniform float shadowRadius;
uniform int canopySteps,shadowFilter;
uniform float shadowWeight,solarTwilight;
vec3 canopyCoord(vec3 point){
    vec3 p=point+shadowOffset;
    return vec3(vec2(dot(p,sunRight),dot(p,sunUp))/(2.0*shadowRadius)+.5,
        .5-dot(p,sunDir)/200.0);
}
float shadowTap(vec3 uv,vec2 delta,float bias){
    return texture(canopyMap,vec3(uv.xy+delta,uv.z-bias));
}
float mapEdge(vec3 uv){
    return 1.0-smoothstep(.43,.49,max(abs(uv.x-.5),abs(uv.y-.5)));
}
// Receiver-plane depth correction: compare each tap against its own
// projected receiver depth. Material relief must not affect shadow bias.
vec2 receiverSlope(vec3 normal,float radius,float depthRange){
    float nz=dot(normal,sunDir);
    float safe=(nz<0.?-1.:1.)*max(abs(nz),.15);
    return vec2(dot(normal,sunRight),dot(normal,sunUp))*(radius/depthRange)/safe;
}
float compareShadow(sampler2DShadow map,vec3 uv,vec2 delta,vec2 slope,float bias){
    return texture(map,vec3(uv.xy+delta,uv.z+dot(slope,delta)-bias));
}
float filteredShadow(sampler2DShadow map,vec3 uv,vec3 normal,float radius,float depthRange,int quality){
    float edge=mapEdge(uv);
    if(edge<=0. || uv.z<=0. || uv.z>=1.)return 1.;
    vec2 texel=1./vec2(textureSize(map,0));
    vec2 slope=receiverSlope(normal,radius,depthRange);
    // Cover the hardware bilinear footprint and residual depth quantization.
    float bias=1.05*dot(abs(slope),texel)+max(4./16777216.,.18*radius*texel.x/depthRange);
    float sum=0.,weight=0.;
    if(quality==0)sum=compareShadow(map,uv,vec2(0),slope,bias);
    else if(quality==1){
        sum=.25*(compareShadow(map,uv,texel*vec2(-.65,-.65),slope,bias)
            +compareShadow(map,uv,texel*vec2(.65,-.65),slope,bias)
            +compareShadow(map,uv,texel*vec2(-.65,.65),slope,bias)
            +compareShadow(map,uv,texel*vec2(.65,.65),slope,bias));
    }else{
        int k=quality>=3?2:1;
        for(int y=-k;y<=k;++y)for(int x=-k;x<=k;++x){
            float w=1./(1.+float(x*x+y*y)*.35);
            sum+=w*compareShadow(map,uv,texel*vec2(x,y),slope,bias);weight+=w;
        }
        sum/=weight;
    }
    return mix(1.,sum,edge);
}
float treeSun(vec3 point,vec3 normal){
    return filteredShadow(canopyMap,canopyCoord(point),normal,shadowRadius,100.,shadowFilter);
}
float canopySun(vec3 point,vec3 normal){
    float tree=treeSun(point,normal);
    if(!creatureActive || tree<=0.0)return tree;
    vec3 p=point+creatureOffset;
    vec3 uv=vec3(vec2(dot(p,sunRight),dot(p,sunUp))/(2.*creatureRadius)+.5,.5-dot(p,sunDir)/(2.*creatureDepthRange));
    float creature=filteredShadow(creatureMap,uv,normal,creatureRadius,creatureDepthRange,min(shadowFilter,1));
    return min(tree,creature);
}
// Ten fixed-cost hardware-filtered samples of occluded sunlight along the eye ray.
// No scattering beyond an opaque surface; no ray/triangle intersection work.
vec3 canopyAir(vec3 ray,float travel){
    if(!canopyActive || canopySteps<=0 || travel<.15 || shadowWeight<.001)return vec3(0.0);
    float limit=min(travel,45.0);
    // Maintain the requested spatial sample density instead of doing all 32
    // taps even for a nearby opaque surface. Full-range rays retain quality.
    int steps=min(canopySteps,max(2,int(ceil(float(canopySteps)*limit/45.0))));
    float stepLength=limit/float(steps);
    float sum=0.0;
    // Interleaved screen-space jitter trades visible slices for fine grain.
    float jitter=fract(52.9829189*fract(dot(gl_FragCoord.xy,vec2(.06711056,.00583715))));
    // Project the ray once; the atlas transform is affine. The attenuation
    // is a geometric progression, with the same jitter and sample positions.
    float first=(.20+.60*jitter)*stepLength;
    vec3 uv=canopyCoord(ray*first);
    vec3 increment=vec3(dot(ray,sunRight)/(2.0*shadowRadius),
        dot(ray,sunUp)/(2.0*shadowRadius),-dot(ray,sunDir)/200.0)*stepLength;
    float attenuation=exp(-first*.032),decay=exp(-stepLength*.032);
    for(int i=0;i<32;i++,uv+=increment,attenuation*=decay){
        if(i>=steps)break;
        float edge=mapEdge(uv);
        if(edge<=0.0 || uv.z<=0.0 || uv.z>=1.0)continue;
        float lit=shadowTap(uv,vec2(0.0),.0008);
        sum+=lit*edge*attenuation;
    }
    float phase=.43+.57*pow(max(dot(ray,sunDir),0.0),3.0);
    return mix(vec3(1.0,.81,.48),vec3(1.0,.46,.20),solarTwilight)*sum*stepLength*.025*phase*shadowWeight;
}
)GLSL";
std::string withCanopy(const char* shader){
    std::string source(shader);
    source.insert(source.find('\n')+1,canopyLighting);
    return source;
}
constexpr const char* fieldVertex=R"GLSL(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
layout(location=3) in float material;
layout(location=4) in vec3 shade;
uniform vec3 origin, originRemainder, forward, right, up;
uniform vec2 rotation,tilt;
uniform float gait;
uniform float scale, aspect, zNear, zFar, fov, ambient;
out vec3 surface,terrainPosition,n,sunlight;
flat out vec3 tint;
out vec3 skinTint;
uniform vec3 terrainOrigin;
uniform vec3 fieldSun;
out vec2 tex;
flat out int layer;
out float distanceToEye;
void main() {
    mat3 rot=mat3(rotation.x,0,-rotation.y, 0,1,0, rotation.y,0,rotation.x);
    mat3 incline=mat3(1,0,0, 0,tilt.x,-tilt.y, 0,tilt.y,tilt.x);
    vec3 local=position;
    if(gait!=0.0)local.z+=gait*sign(local.x)*clamp((0.68-local.y)/0.68,0.0,1.0);
    surface=(origin+rot*incline*local*scale)+originRemainder;
    terrainPosition=terrainOrigin+rot*incline*local*scale;
    float d=dot(surface,forward);
    gl_Position=vec4(dot(surface,right)*fov/aspect,dot(surface,up)*fov,
        (zFar+zNear)/(zFar-zNear)*d-(2*zFar*zNear)/(zFar-zNear),d);
    n=rot*incline*normal; tint=shade; skinTint=shade; tex=uv; layer=int(material);
    float sun=max(dot(n,fieldSun),0.0);
    vec3 bounce=mix(vec3(0.68,0.59,0.51),vec3(0.80,0.88,0.98),0.5+0.5*n.y);
    sunlight=ambient*(bounce+vec3(0.41,0.36,0.29)*sun);
    distanceToEye=d;
}
)GLSL";
constexpr const char* fieldFragment=R"GLSL(#version 330 core
in vec3 surface,terrainPosition,n,sunlight;
flat in vec3 tint;
in vec3 skinTint;
uniform bool terrainSurface,twoSidedLeaf;
uniform sampler3D geology;
uniform vec3 detailPhase0,detailPhase1,woodPhase0,woodPhase1;
uniform float detailFrequency,detailBlend,detailAmount,detailInvBody;
uniform int surfaceQuality;

in vec2 tex;
flat in int layer;
in float distanceToEye;
uniform sampler2DArray materials;
uniform sampler2D northSkin;
uniform vec3 sky;
uniform vec4 lamps[8];
uniform float trailRadius;
uniform int lightCount;
uniform vec3 gateLights[2];
uniform int gateLightCount;
uniform float effectTime;
uniform vec3 beamDirection;
uniform float beamReach;
#ifdef CLIPPED
uniform vec3 cutLo,cutHi;
uniform int cut;
uniform float cutOwnerInset;
#endif
uniform float fog,emissive,cellGrid,lodBias;
uniform float ambient;
uniform vec3 fieldSun;
#ifdef LOG_DEPTH
uniform float depthNear,depthNormalization;
#endif
out vec4 pixel;
// The noise is a volume in world units. A newly cut wall samples the same
// folded beds as its floor and neighboring chunks, at every mesh resolution.
vec3 groundColor(vec3 p,vec3 normal,out float relief){
    float drift=texture(geology,p*(1.8/64.0)).r;
    if(layer==8){
        // Three scales of longitudinal bark, not isotropic brown rock.
        float broad=texture(geology,p*vec3(1.35,.065,1.35)/64.0).r;
        vec3 warp=p+vec3((broad-.5)*.31,0,(drift-.5)*.31);
        float folds=texture(geology,warp*vec3(5.8,.19,5.8)/64.0).r;
        float fibers=surfaceQuality>=2?texture(geology,warp*vec3(24.0,.95,24.0)/64.0).r:folds;
        relief=folds*.65+fibers*.35;
        float furrow=smoothstep(.20,.78,folds+.22*(fibers-.5));
        vec3 darkBark=vec3(.19,.115,.065), lightBark=vec3(.49,.34,.22);
        vec3 bark=mix(darkBark,lightBark,furrow)*(.84+.31*broad);
        bark+=vec3(.10,.062,.031)*(fibers-.5);
        float moss=smoothstep(.62,.82,texture(geology,p*(1.1/64.0)+vec3(.13,.43,.27)).r);
        return mix(bark,vec3(.24,.29,.12),moss*(.22+.33*max(normal.y,0.0)))*(.84+.30*fibers);
    }
    else if(layer==9){
        // Deep forest greens, fresh tips, small clusters and fine veins.
        float mass=texture(geology,p*vec3(.76,1.1,.76)/64.0).r;
        float sprays=texture(geology,p*vec3(4.8,6.5,4.8)/64.0+vec3(.1,.48,.23)).r;
        float blades=texture(geology,p*vec3(17.5,22.0,17.5)/64.0).r;
        float veins=surfaceQuality>=2?texture(geology,p*vec3(52.0,61.0,52.0)/64.0).r:blades;
        float value=.25*mass+.50*sprays+.25*blades;
        vec3 deep=vec3(.035,.105,.032),middle=vec3(.13,.28,.060),tip=vec3(.32,.47,.15);
        vec3 pigment=mix(deep,middle,smoothstep(.19,.55,value));
        pigment=mix(pigment,tip,smoothstep(.52,.83,value));
        pigment*=.74+.28*veins+.06*max(0.0,normal.y);
        relief=.20*sprays+.55*blades+.25*veins;
        return pigment;
    }
    vec3 folded=p+vec3(0.18,0.32,-0.13)*(drift-0.5);
    float body=texture(geology,folded*(5.0/64.0)).r;
    float fleck=texture(geology,folded*(24.0/64.0)).r;
    float sand=surfaceQuality>=2?texture(geology,p*(92.0/64.0)).r:fleck;
    relief=.20*body+.48*fleck+.32*sand;
    // Geological veins follow warped *three-dimensional* strata. The prior
    // cosine of x/y/z painted broad concentric rings on horizontal dirt.
    float bed=0.0;
    vec3 stone=vec3(0.0);
    if(layer==3 || layer==4 || layer==5 || layer==6 || layer==7 || layer==13 || layer==15){
        float strata=texture(geology,vec3(folded.x*0.07+body*4.0,
            folded.y*0.52+drift*1.9,folded.z*0.07+body*3.0)).r;
        bed=smoothstep(0.63,0.77,strata);
        float seam=(1.0-smoothstep(0.34,0.72,abs(body-fleck)))*(0.18+0.43*bed);
        stone=mix(vec3(0.30,0.32,0.33),vec3(0.55,0.55,0.51),0.58*body+0.42*drift);
        stone+=vec3(0.043,0.039,0.032)*seam;
    }
    vec3 pigment=stone;
    if(layer==4 || layer==5 || layer==6 || layer==7 || layer==13){
        float lens=texture(geology,(p*vec3(3.0,7.0,3.5)+vec3(2.3,5.1,1.7)*drift)/64.0).r;
        float ore=smoothstep(0.35,0.71,0.58*lens+0.23*body+0.12*fleck+0.07*bed);
        vec3 mineral=layer==4?vec3(0.65,0.39,0.19):layer==5?vec3(0.64,0.31,0.15):
            layer==6?vec3(0.12,0.14,0.15):layer==7?vec3(0.24,0.26,0.33):vec3(0.83,0.80,0.74);
        pigment=mix(stone,mineral,ore);
    }else if(layer==1 || layer==42){
        vec3 soil=mix(vec3(0.29,0.19,0.125),vec3(0.49,0.34,0.21),body);
        // Cooler hillside greens and small mottled patches, using the existing
        // noise samples rather than extra texture lookups or a new atlas.
        float meadow=smoothstep(.25,.78,drift);
        vec3 turf=mix(vec3(.105,.19,.055),vec3(.36,.43,.16),.65*body+.35*drift);
        turf=mix(turf,turf*vec3(.94,1.07,.91),meadow*.65);
        turf*=.77+.26*fleck+.15*sand;
        if(surfaceQuality>=2)turf*=.88+.20*smoothstep(.28,.74,sand);
        // Grass retreats from nearly vertical banks and exposed cut faces.
        float green=smoothstep(0.26,0.83,normal.y+0.07*(fleck-0.5));
        pigment=mix(soil,turf,green);
        if(layer==42){
            // Irregular worn earth with small gravel and grass intrusions.
            // tex.x is the continuous signed path edge sampled on the roof.
            float irregular=texture(geology,p*vec3(.21,.11,.21)+vec3(.27,.61,.42)).r;
            float edge=tex.x+(irregular-.5)*.55;
            float worn=smoothstep(-.50,.42,edge);
            float grit=texture(geology,p*vec3(1.1,.30,1.1)).r;
            float gravel=smoothstep(.64,.85,grit)*smoothstep(.38,.70,fleck);
            vec3 earth=mix(vec3(.29,.225,.155),vec3(.48,.385,.26),.62*body+.38*fleck);
            earth=mix(earth,vec3(.59,.54,.43),gravel*.60);
            earth*=.87+.24*grit;
            pigment=mix(pigment,earth,worn);
            relief=mix(relief,.45*grit+.35*fleck+.20*sand,worn);
        }
    }else if(layer==2){
        float grain=texture(geology,(p*vec3(11.0,3.1,10.8)+vec3(body*2.7))/64.0).r;
        pigment=mix(vec3(0.31,0.21,0.145),vec3(0.51,0.37,0.25),0.55*body+0.45*grain);
    }
    else if(layer==10)pigment=mix(vec3(0.45,0.28,0.22),vec3(0.64,0.43,0.32),body);
    else if(layer==12)pigment=mix(vec3(0.63,0.53,0.36),vec3(0.82,0.74,0.54),body);
    else if(layer==15)pigment=stone*0.64;
    return pigment*(0.82+0.22*fleck+0.17*sand);
}
// Stable world-space microstructure. Frequency changes only by fixed powers
// of four; the fractional zoom blends adjacent bands rather than stretching
// a texture with the camera. CPU phase retains precision at microscopic tiers.
vec3 microstructure(vec3 pigment){
    if(detailAmount<=0.0)return pigment;
    float distanceFade=1.0-smoothstep(5.0,24.0,length(surface)*detailInvBody);
    vec3 anisotropy=layer==8?vec3(1.0,.125,1.0):vec3(1.0);
    vec3 phase0=layer==8?woodPhase0:detailPhase0;
    vec3 phase1=layer==8?woodPhase1:detailPhase1;
    vec3 q=surface*anisotropy*detailFrequency;
    // Explicit gradients keep the volume mip filter stable near transitions.
    vec3 dx=dFdx(surface)*anisotropy*detailFrequency;
    vec3 dy=dFdy(surface)*anisotropy*detailFrequency;
    if(distanceFade<=0.0)return pigment;
    float coarse=textureGrad(geology,q+phase0,dx,dy).r;
    float fine=textureGrad(geology,q*4.0+phase1,dx*4.0,dy*4.0).r;
    float grain=mix(coarse,fine,detailBlend);
    float contrast=layer==1 || layer==9?.72:layer==8?.87:1.0;
    float amount=detailAmount*distanceFade*contrast;
    float relief=(grain-.5)*1.10;
    vec3 shade=vec3(relief*.95,relief,relief*1.05);
    if(layer==1 || layer==9)shade+=vec3(.06,.12,-.035)*(grain-.5);
    return max(vec3(0.0),pigment*(vec3(1.0)+amount*shade));
}
// Screen-space derivatives create relief without extra collision geometry.
// Derivative fade keeps the grain stable as the player walks away.
vec3 materialNormal(vec3 p,vec3 normal,float height,float strength){
    vec3 dx=dFdx(p),dy=dFdy(p);
    vec3 a=cross(dy,normal),b=cross(normal,dx);
    float det=dot(dx,a);
    float footprint=max(length(dx),length(dy));
    if(abs(det)<1e-12)return normal;
    vec3 gradient=sign(det)*(dFdx(height)*a+dFdy(height)*b);
    float fade=1.0-smoothstep(.028,.22,footprint);
    vec3 slope=strength*fade*gradient/max(abs(det),1e-12);
    slope*=min(1.,.65/max(length(slope),1e-6));
    return normalize(normal-slope*.75);
}

void main() {
    // The duplicate reversed leaf triangles previously provided exactly this
    // normal. The canopy shell and all other geometry remain single sided.
    vec3 faceNormal=twoSidedLeaf && !gl_FrontFacing?-n:n;
    // Evaluate before material-dependent branching; use the true receiver plane.
    vec3 plane=cross(dFdx(surface),dFdy(surface));
    vec3 geometricNormal=normalize(faceNormal);
    float planeLength=length(plane);
    if(planeLength>1e-20){plane/=planeLength;geometricNormal=dot(plane,geometricNormal)<0.?-plane:plane;}
#ifdef CLIPPED
    // Classify the solid cell behind the face, including faces on the cut plane.
    vec3 owner=surface-faceNormal*cutOwnerInset;
    bool inside=all(greaterThanEqual(owner,cutLo)) && all(lessThan(owner,cutHi));
    if((cut==1 && inside) || (cut==2 && !inside)) discard;
#endif
#ifdef LOG_DEPTH
    gl_FragDepth=clamp(log2(max(distanceToEye/depthNear,1.0))*depthNormalization,0.0,1.0);
#endif
    if(distanceToEye>=fog){pixel=vec4(sky+canopyAir(normalize(surface),45.0),1);return;}
    // Dedicated first-person artwork materials. Only the rifle uses these
    // layers; they do not alter the world atlas or the environmental shaders.
    if(layer==48){pixel=vec4(tint,.14);return;}
    if((layer>=45 && layer<=47) || (layer>=49 && layer<=52)){
        if(layer==52){pixel=vec4(tint*1.35,1.0);return;}
        if(layer==46){pixel=vec4(tint*(1.25+1.25*emissive),1.0);return;}
        vec3 N=normalize(faceNormal),V=normalize(-surface);
        float broad=texture(geology,vec3(tex*.46,.39)).r;
        float grain=texture(geology,vec3(tex*4.7,.71)).r;
        float scratches=texture(geology,vec3(tex*vec2(1.8,12.0),.17)).r;
        float facing=abs(dot(N,V));
        float key=.35+.48*max(0.,dot(N,normalize(fieldSun)))+.17*max(N.y,0.);
        float wear=smoothstep(.64,.91,scratches)*.11;
        vec3 base=tint*(.73+.31*broad+.15*grain);
        if(layer==45){
            float sheen=pow(1.-facing,2.)*.14+pow(max(0.,dot(N,normalize(V+fieldSun))),20.)*.22;
            base=base*(.52+key*.94)+vec3(.76,.70,.64)*(sheen+wear)*(.4+.6*ambient);
        }else if(layer==47){
            base*=.55+.65*key+.12*grain;
            base+=vec3(.032,.023,.016)*smoothstep(.48,.85,scratches);
        }else if(layer==49){
            // Technical woven textile: mip-friendly alternating diagonal yarns.
            vec2 uv=tex*115.0;
            float filterWidth=max(length(dFdx(uv)),length(dFdy(uv)));
            float braid=sin((uv.x+uv.y)*6.283185)*sin((uv.x-uv.y)*6.283185);
            braid*=1.-smoothstep(.23,.85,filterWidth);
            base=tint*(.66+.37*key+.18*broad+.14*braid);
            base+=vec3(.035,.04,.052)*pow(1.-facing,3.)*.50;
        }else if(layer==50){
            // Satin black ceramic, not metallic rifle wear. Tight specular
            // response and subtle micrograin articulate each armored panel.
            float spec=pow(max(0.,dot(N,normalize(V+normalize(fieldSun)))),32.);
            base=tint*(.44+.80*key+.15*grain)+vec3(.12,.14,.19)*(spec*.46+pow(1.-facing,3.)*.28);
        }else{
            // Flexible grip pads and finger articulation bellows.
            base=tint*(.64+.45*key+.25*grain)+vec3(.022,.025,.033)*pow(1.-facing,3.);
        }
        // Keep the artwork legible under its own accent lights, but respond
        // to the surrounding day/night lighting and nearby trail lanterns.
        vec3 illumination=vec3(.40+.60*ambient);
        for(int i=0;i<lightCount;++i){
            vec3 delta=lamps[i].xyz-surface;float d2=dot(delta,delta);
            if(lamps[i].w<0.0){
                float a=max(0.,1.-d2/(trailRadius*trailRadius));
                illumination+=vec3(1.,.67,.32)*(-lamps[i].w)*a*a*.40;
            }else if(d2<30.303)illumination+=vec3(1.,.79,.49)*lamps[i].w*(1.-d2*.033)/(1.+d2*.4)*.45;
        }
        pixel=vec4(base*illumination,1.0);return;
    }
    if(layer==67){
        // Continuous animated portal interior with coherent coordinates.
        vec2 p=tex;float r=length(p),a=atan(p.y,p.x);float t=effectTime;
        float warp=texture(geology,vec3(p*1.8,t*.035)).r;
        float foggy=texture(geology,vec3(p*3.4+vec2(.08,-.12)*t,warp)).r;
        float flow=texture(geology,vec3(p*1.2+vec2(-.031,.023)*t,.32+t*.009)).r;
        float wavy=texture(geology,vec3(p*2.8+vec2(.032,-.040)*t,flow*.35)).r;
        float fil=sin(11.*r-2.*a+t*.75+5.*flow+1.2*sin(a*3.+r*5.));
        float filament=pow(max(0.,fil),5.)*(.25+.75*foggy)*(.45+.55*wavy);
        float cloud=smoothstep(.24,.78,.64*foggy+.36*flow);
        float rim=smoothstep(.72,.99,r);
        float sparks=pow(max(0.,sin(a*19.+t*.7+warp*5.)),28.)*rim;
        vec3 cold=tint*vec3(.74,1.,1.1);
        vec3 rgb=cold*(.055+.25*cloud+.55*filament+rim*.86+sparks*.55)+vec3(.19,.035,.29)*flow*(1.-rim)*(.35+.65*cloud);
        pixel=vec4(mix(rgb,sky,smoothstep(fog*.46,fog,distanceToEye)),1.);return;
    }
    vec3 albedo;
    if(terrainSurface && ((layer>=1 && layer<=13 && layer!=11) || layer==15 || layer==42)){
        float relief;
        albedo=microstructure(groundColor(terrainPosition,normalize(faceNormal),relief));
        // Reuse pigment samples for normal relief rather than duplicate bark fetches.
        if(surfaceQuality==3){
            vec3 q=terrainPosition*(layer==8?vec3(3.8,.16,3.8):vec3(2.5));
            vec3 qx=dFdx(surface)*(layer==8?vec3(3.8,.16,3.8):vec3(2.5));
            vec3 qy=dFdy(surface)*(layer==8?vec3(3.8,.16,3.8):vec3(2.5));
            float footprint=max(length(qx),length(qy));
            float fade=1.-smoothstep(.025,.12,footprint);
            float grit=textureGrad(geology,q+vec3(.19,.37,.67),qx,qy).r;
            albedo*=1.+(grit-.5)*.34*fade;
            relief=mix(relief,grit,.22*fade);
        }
        if(surfaceQuality>0){
            float strength=layer==8?.040:layer==9?.018:layer==1?.016:.025;
            faceNormal=materialNormal(surface,normalize(faceNormal),relief,strength*(surfaceQuality==3?1.45:1.));
        }
    }
    else if(layer==38 || layer==39){
        float patina=texture(northSkin,tex*0.38,lodBias).r;
        if(layer==38)albedo=vec3(0.72,0.75,0.80)*mix(0.58,1.13,patina)*tint;
        else {
            float sinew=texture(northSkin,tex*vec2(0.053,0.31),lodBias).r;
            // Witness skin is ashen and mottled, unlike North's metal.
            albedo=vec3(.76,.75,.70)*mix(.43,1.24,patina*sinew)*skinTint;
        }
    }else if(layer==58 || layer==59 || layer==65 || layer==68){
        albedo=tint;
    }else if(layer>=53 && layer<=57){
        // Character-local procedural materials. Unlike the first-person rifle,
        // these use normal world lighting, including the canopy and lanterns.
        vec3 p=vec3(tex,tex.x*.31-tex.y*.19);
        float grain=texture(geology,p*1.7+vec3(.14,.31,.52)).r;
        float fine=texture(geology,p*9.5+vec3(.39,.11,.27)).r;
        float broad=texture(geology,p*.43).r;
        albedo=tint*(.76+.34*broad+.10*fine);
        if(layer==53)albedo=skinTint*(.86+.20*broad+.08*fine);
        if(layer==54){float strand=texture(geology,p*vec3(19.,1.8,2.)).r;float filaments=.5+.5*sin(tex.x*155.+grain*3.);filaments=mix(filaments,.5,smoothstep(.025,.10,fwidth(tex.x)));albedo=tint*(.53+.29*strand+.17*grain+.16*filaments);}
        if(layer==55)albedo=tint*(.55+.39*broad+.25*fine);
        if(layer==56)albedo=tint*(.60+.37*grain+.13*fine);
        if(layer==57){float weave=sin(tex.x*430.)*sin(tex.y*330.);weave*=1.-smoothstep(.05,.15,max(length(dFdx(tex)),length(dFdy(tex))));albedo=tint*(.72+.30*broad+.11*weave);}
    }else if(layer>=62 && layer<=68){
        vec3 p=vec3(tex,tex.x*.31-tex.y*.19);
        float coarse=texture(geology,p*.52).r,grain=texture(geology,p*3.2).r,fine=texture(geology,p*11.7).r;
        albedo=tint*(.61+.43*coarse+.20*grain+.09*fine);
        if(layer==62){
            float fissure=smoothstep(.76,.90,texture(geology,p*vec3(2.,7.,3.)).r);
            albedo*=1.-.52*fissure;
            if(surfaceQuality>=2){
                float grain2=texture(northSkin,tex*.68,lodBias).r;
                float relief=grain*.54+fine*.30+grain2*.16;
                albedo*=.88+.24*grain2;
                faceNormal=materialNormal(surface,normalize(faceNormal),relief,.009);
            }
        }
        if(layer==64){float thread=sin(tex.x*550.)*sin(tex.y*410.);thread*=1.-smoothstep(.05,.18,max(length(dFdx(tex)),length(dFdy(tex))));albedo*=.79+.19*grain+.09*thread;}
        if(layer==66)albedo*=.85+.30*coarse;
    }else if(layer==60){
        vec3 p=vec3(tex,tex.x*.31-tex.y*.19);
        float broad=texture(geology,p*.83).r;
        float hair=texture(geology,p*vec3(24.,4.,6.)).r;
        float fine=texture(geology,p*vec3(41.,11.,8.)).r;
        float lod=smoothstep(.012,.055,max(length(dFdx(tex)),length(dFdy(tex))));
        albedo=skinTint*(.77+.28*broad+.26*mix(hair,.5,lod)+.10*mix(fine,.5,lod));
    }else if(layer==61){
        float mottled=texture(geology,vec3(tex,tex.x*.31)*9.).r;
        albedo=tint*(.62+.51*mottled);
    }else if(layer==44){
        vec3 uvw=vec3(tex*vec2(1.6,.85),.37);
        float worn=texture(geology,uvw*.53).r;
        float nap=texture(geology,uvw*vec3(2.7,.24,1)).r;
        albedo=skinTint*(.58+.30*worn+.23*nap);
    }else if(layer==43){
        // Smooth interpolation keeps the iris from becoming a polygon mosaic.
        albedo=skinTint;
    }else if(layer==40 || layer==41){
        // Independent bone material, sampled in head-local coordinates.
        // Mipmapped volume noise gives dry mineral grain instead of sinew.
        if(layer==40){
            vec3 p=vec3(tex,tex.x*.31-tex.y*.19);
            float weather=texture(geology,p*.30+vec3(.17,.38,.51)).r;
            float grain=texture(geology,p*1.30+vec3(.36,.10,.29)).r;
            float pores=texture(geology,p*5.8+vec3(.08,.63,.32)).r;
            float pit=1.0-smoothstep(.19,.44,pores);
            float dirt=smoothstep(.52,.80,weather)*.46;
            vec3 bone=skinTint*(.57+.55*weather+.24*grain-.14*pit);
            albedo=mix(bone,bone*vec3(.54,.48,.40),dirt);
            // Very shallow relief, using derivatives and existing mipmaps.
            // This modulates diffuse lighting, not vertex silhouette or gloss.
            vec3 normal=normalize(faceNormal);
            vec3 dx=dFdx(surface),dy=dFdy(surface);
            vec3 a=cross(dy,normal),b=cross(normal,dx);
            float det=dot(dx,a);
            float uvStep=max(length(dFdx(tex)),length(dFdy(tex)));
            float worldStep=max(length(dx),length(dy));
            float unitScale=worldStep/max(uvStep,1e-6);
            float micrograin=surfaceQuality>=2?texture(geology,p*16.0+vec3(.31,.11,.43)).r:pores;
            float height=grain*.54+pores*.34+micrograin*.12;
            albedo*=1.-.095*(1.-smoothstep(.19,.42,micrograin))*(1.-smoothstep(.003,.015,uvStep));
            if(abs(det)>1e-14){
                vec3 gradient=sign(det)*(dFdx(height)*a+dFdy(height)*b);
                vec3 slope=.0030*unitScale*gradient/max(abs(det),1e-14);
                slope*=min(1.,.48/max(length(slope),1e-6));
                vec3 relief=normalize(normal-slope);
                faceNormal=normalize(mix(normal,relief,.85*(1.0-smoothstep(.006,.025,uvStep))));
            }
        }else albedo=tint*vec3(.95,.90,.83);
    }else albedo=texture(materials,vec3(tex,float(layer)),lodBias).rgb*tint;
    if(terrainSurface && layer==9)albedo*=tint;
    if(cellGrid>0.0 && !terrainSurface) {
        vec2 uv=fract(tex);
        vec2 range=min(uv,1-uv);
        float distanceToEdge=min(range.x,range.y);
        float sharpness=1.0-clamp(max(fwidth(tex.x),fwidth(tex.y))*1.2,0.0,1.0);
        albedo*=1.0-cellGrid*sharpness*(1-smoothstep(0.005,0.055,distanceToEdge));
    }
    vec3 light=sunlight;
    if(layer==40 || layer==43){
        vec3 normal=normalize(faceNormal);
        vec3 bounce=mix(vec3(.68,.59,.51),vec3(.80,.88,.98),.5+.5*normal.y);
        light=ambient*(bounce+vec3(.41,.36,.29)*max(dot(normal,fieldSun),0.0));
    }
    if(terrainSurface && ((layer>=1 && layer<=13 && layer!=11) || layer==15 || layer==42)){
        vec3 normal=normalize(faceNormal);
        vec3 sun=fieldSun;
        float diffuse=max(dot(normal,sun),0.0);
        vec3 bounce=mix(vec3(.68,.59,.51),vec3(.80,.88,.98),.5+.5*normal.y);
        light=ambient*(bounce+vec3(.41,.36,.29)*diffuse);
        if(layer==9)light+=ambient*vec3(.12,.17,.055)*max(dot(-normal,sun),0.0);
    }
    float directLight=1.0;
    if(canopyActive){
        float direct=canopySun(surface,geometricNormal);
        directLight=mix(1.0,direct,shadowWeight);
        // Retain cool sky bounce in shadow; warm direct light only through
        // openings. Local lamps and the torch are added afterwards.
        light*=mix(vec3(1.0),mix(vec3(.25,.32,.38),vec3(1.04,1.00,.93),direct),shadowWeight);
    }
    for(int i=0;i<lightCount;i++) {
        vec3 delta=lamps[i].xyz-surface;
        float d2=dot(delta,delta);
        // Trail lanterns are encoded as negative strength and use a shared
        // 6.4m radius. Their overlap remains visibly lit at a 9m interval.
        if(lamps[i].w<0.0){
            if(d2>=trailRadius*trailRadius)continue;
            float fall=1.0-d2/(trailRadius*trailRadius);
            float facing=.48+.52*max(0.0,dot(normalize(faceNormal),delta/max(sqrt(d2),.001)));
            light+=vec3(1.0,.68,.34)*(-lamps[i].w)*fall*fall*facing;
        }else if(d2<30.303)
            light+=vec3(1.0,0.79,0.49)*lamps[i].w*(1.0-d2*0.033)/(1.0+d2*0.4);
    }
    for(int i=0;i<gateLightCount;i++){
        vec3 delta=gateLights[i]-surface;float d2=dot(delta,delta);
        if(d2<30.){float fall=1.-d2/30.;float facing=.32+.68*max(0.,dot(normalize(faceNormal),delta/max(sqrt(d2),.001)));
            light+=vec3(.13,.65,.81)*fall*fall*facing*(1.0+.09*sin(effectTime*2.1));}
    }
    if(beamReach>0.0){
        float d2=dot(surface,surface);
        if(d2<beamReach*beamReach && d2>1e-16){
            vec3 ray=surface*inversesqrt(d2);
            float edge=smoothstep(0.84,0.965,dot(ray,beamDirection));
            float fall=1.0-d2/(beamReach*beamReach);
            light+=vec3(1.18,1.08,0.90)*edge*fall*(0.24+0.76*max(dot(faceNormal,-ray),0.0));
        }
    }
    float haze=smoothstep(fog*0.46,fog,distanceToEye);
    vec3 lit=albedo*max(light,vec3(emissive));
    if(layer==43){
        vec3 eyeNormal=normalize(faceNormal),v=normalize(-surface);
        vec3 halfSum=v+fieldSun;
        vec3 halfVector=halfSum/max(length(halfSum),.0001);
        float direct=directLight;
        float glint=pow(max(0.0,dot(halfVector,eyeNormal)),160.0)*ambient*direct*.42;
        if(beamReach>0.0){
            float d2=dot(surface,surface);
            if(d2>1e-16 && d2<beamReach*beamReach){
                float cone=smoothstep(.84,.965,dot(-v,beamDirection));
                glint+=pow(max(0.0,dot(v,eyeNormal)),110.0)*cone*(1.0-d2/(beamReach*beamReach))*.32;
            }
        }
        lit+=vec3(.80,.78,.66)*glint;
    }
    if(layer==53 || layer==54 || layer==56 || layer==59){
        vec3 N=normalize(faceNormal),V=normalize(-surface),H=normalize(V+fieldSun);
        float shine=pow(max(0.,dot(N,H)),layer==59?65.:layer==56?22.:layer==53?32.:13.);
        float direct=directLight;
        lit+=vec3(1.,.72,.42)*shine*ambient*direct*(layer==56?.40:layer==59?.15:layer==53?.055:.085);
    }
    if(layer==56){
        // Bronze edge highlights respond to the actual nearby lamps as well
        // as the sun, without making the armor itself an emissive surface.
        vec3 N=normalize(faceNormal),V=normalize(-surface);
        float rim=pow(1.-abs(dot(N,V)),3.);
        lit+=albedo*rim*ambient*.12;
        for(int i=0;i<lightCount;i++){
            vec3 delta=lamps[i].xyz-surface;float d2=dot(delta,delta);
            if(d2<.0001 || d2>trailRadius*trailRadius)continue;
            vec3 H=normalize(V+delta/sqrt(d2));float spec=pow(max(dot(N,H),0.),26.);
            float power=lamps[i].w<0.?-lamps[i].w*pow(max(0.,1.-d2/(trailRadius*trailRadius)),2.):max(0.,lamps[i].w)/(1.+d2*.4);
            lit+=vec3(.70,.43,.20)*spec*power*.18;
        }
    }
    if(layer==60 || layer==61){
        vec3 N=normalize(faceNormal),V=normalize(-surface);
        vec3 H=normalize(V+fieldSun);
        float direct=directLight;
        float highlight=pow(max(dot(N,H),0.),layer==61?32.:7.);
        lit+=vec3(.74,.51,.35)*highlight*ambient*direct*(layer==61?.16:.035);
    }
    if(layer==63 || layer==66){
        vec3 N=normalize(faceNormal),V=normalize(-surface),H=normalize(V+fieldSun);
        lit+=vec3(.55,.42,.28)*pow(max(dot(N,H),0.),28.)*ambient*.15;
    }
    // Bounded rough-surface specular response at high material quality.
    if(surfaceQuality==3 && (terrainSurface || layer==40 || layer==62 || layer==66)){
        vec3 N=normalize(faceNormal),V=normalize(-surface),L=normalize(fieldSun);
        vec3 H=normalize(V+L+vec3(1e-6));
        float rough=layer==40?.64:layer==8?.82:layer==9?.76:layer==66?.83:.91;
        float nh=max(0.,dot(N,H)),nv=max(.04,dot(N,V)),nl=max(0.,dot(N,L));
        float a2=pow(rough,4.0),d=nh*nh*(a2-1.)+1.;
        float distribution=a2/(3.14159*d*d),k=(rough+1.)*(rough+1.)*.125;
        float visibility=(nv/(nv*(1.-k)+k))*(nl/(nl*(1.-k)+k));
        float fresnel=.035+.965*pow(1.-clamp(dot(V,H),0.,1.),5.);
        float spec=min(.16,distribution*visibility*fresnel/max(4.*nv*nl,.04));
        lit+=vec3(1.,.88,.70)*spec*nl*ambient*directLight*.72;
        if(layer==9)lit+=albedo*vec3(.58,.78,.19)*pow(max(0.,dot(-V,L)),3.)*ambient*directLight*.28;
    }
    if(layer==65)lit=max(lit,tint*(.70+.20*sin(effectTime*2.3+tex.y*4.)));
    if(layer==68)lit=max(lit,tint*(1.05+.30*sin(effectTime*1.8+tex.x*3.)));
    if(layer==58)lit=max(lit,tint*1.15);
    // Compress washed-out diffuse highlights while leaving emissive materials bright.
    float peak=max(lit.r,max(lit.g,lit.b));
    vec3 graded=lit*(vec3(1.06,1.04,1.025)-0.08*min(lit,1.0));
    if(emissive<.8 && layer!=58 && layer!=65 && layer!=68)
        graded/=1.+.40*max(0.,peak-.80);
    vec3 color=mix(graded,sky,haze);
    if(canopyActive){
        float travel=length(surface);
        vec3 air=canopyAir(surface/max(travel,.001),travel);
        color+=air;
    }
    pixel=vec4(color,1.0);
}
)GLSL";
constexpr const char* skyVertex=R"GLSL(#version 330 core
out vec2 screen;
void main(){
    screen=vec2(gl_VertexID==1?3.0:-1.0,gl_VertexID==2?3.0:-1.0);
    gl_Position=vec4(screen,1.0,1.0);
}
)GLSL";
constexpr const char* postFragment=R"GLSL(#version 330 core
in vec2 screen;
out vec4 pixel;
uniform sampler2D scene,sceneDepth;
uniform vec2 texel;
uniform int aa,bloom,occlusion,logDepth,hdr,bloomQuality;
uniform float exposure,bloomThreshold;
uniform vec2 outputTexel;
uniform float nearPlane,farPlane,aspect,fov;
float depthAt(vec2 uv){
    float z=texture(sceneDepth,uv).r;
    if(z>=1.)return farPlane;
    return logDepth!=0?nearPlane*exp2(z*log2(farPlane/nearPlane)):
        nearPlane/(1.-z+z*(nearPlane/farPlane));
}
vec3 viewPoint(vec2 uv,float z){return vec3((uv*2.-1.)*vec2(aspect,1.)*z/fov,z);}
vec2 depthCenter(vec2 uv){return (floor(uv/texel)+.5)*texel;}
vec3 depthPoint(vec2 uv){uv=clamp(depthCenter(uv),texel*.5,1.-texel*.5);return viewPoint(uv,depthAt(uv));}
float ambientOcclusion(vec2 uv){
    uv=clamp(depthCenter(uv),texel*.5,1.-texel*.5);
    float z=depthAt(uv);
    if(z>=farPlane*.98)return 0.;
    vec3 p=viewPoint(uv,z);
    // One-sided gradients on the real depth texel grid, avoiding derivatives
    // across silhouettes and repeated depth values at non-native resolution.
    vec3 left=p-depthPoint(uv-vec2(texel.x,0.)),right=depthPoint(uv+vec2(texel.x,0.))-p;
    vec3 down=p-depthPoint(uv-vec2(0.,texel.y)),up=depthPoint(uv+vec2(0.,texel.y))-p;
    vec3 dx=abs(left.z)<abs(right.z)?left:right,dy=abs(down.z)<abs(up.z)?down:up;
    if(abs(dx.z)>z*.12 || abs(dy.z)>z*.12)return 0.;
    vec3 crossNormal=cross(dy,dx);float normalLength=length(crossNormal);
    if(normalLength<1e-20)return 0.;
    vec3 normal=crossNormal/normalLength;
    float radius=clamp(z*.035,nearPlane*4.,max(nearPlane*4.,1.6*max(nearPlane/.025,.001)));
    vec2 extent=vec2(fov/aspect,fov)*radius/(2.*z);
    extent=clamp(extent,texel*2.,texel*24.);
    float sum=0.;int count=occlusion==1?4:occlusion==2?8:occlusion==3?12:24;
    float precisionStep=logDepth!=0?z*log(farPlane/nearPlane)*2e-7:z*z/nearPlane*2e-7;
    for(int i=0;i<24;++i){
        if(i>=count)break;
        float a=float(i)*2.39996323;
        vec2 q=uv+vec2(cos(a),sin(a))*extent*sqrt((float(i)+.65)/float(count));
        if(any(lessThan(q,texel*.5)) || any(greaterThan(q,1.-texel*.5)))continue;
        vec3 delta=depthPoint(q)-p;
        float d=length(delta);
        if(d<radius*1.6 && d>max(radius*.025,precisionStep)){
            float height=dot(normal,delta)-precisionStep;
            sum+=max(0.,height/d-.14)*(1.-smoothstep(radius*.25,radius*1.6,d));
        }
    }
    return clamp(sum/float(count)*1.55,0.,.30);
}
float luma(vec3 c){return dot(c,vec3(.299,.587,.114));}
vec3 bright(vec3 c){return c*smoothstep(bloomThreshold,bloomThreshold+.34,max(c.r,max(c.g,c.b)));}
void main(){
    vec2 uv=screen*.5+.5;
    vec3 center=texture(scene,uv).rgb;
    vec3 result=center;
    // Four stratified samples cover the destination pixel at supersampled
    // resolutions. Default native resolution is untouched.
    if(outputTexel.x>texel.x*1.01){
        vec2 d=outputTexel*.25;
        result=.25*(texture(scene,uv+vec2(d.x,d.y)).rgb+texture(scene,uv+vec2(-d.x,d.y)).rgb+
            texture(scene,uv+vec2(d.x,-d.y)).rgb+texture(scene,uv-d).rgb);
    }
    if(aa==1 || aa==5){
        vec3 nw=texture(scene,uv+texel*vec2(-1,1)).rgb;
        vec3 ne=texture(scene,uv+texel*vec2(1,1)).rgb;
        vec3 sw=texture(scene,uv+texel*vec2(-1,-1)).rgb;
        vec3 se=texture(scene,uv+texel*vec2(1,-1)).rgb;
        float a=luma(nw),b=luma(ne),c=luma(sw),d=luma(se),m=luma(center);
        float low=min(m,min(min(a,b),min(c,d))),high=max(m,max(max(a,b),max(c,d)));
        if(high-low>max(.025,high*.12)){
            vec2 direction=vec2(-((a+b)-(c+d)),(a+c)-(b+d));
            float reduce=max((a+b+c+d)*.03125,.0078125);
            direction=clamp(direction/(min(abs(direction.x),abs(direction.y))+reduce),vec2(-8),vec2(8))*texel;
            vec3 inner=.5*(texture(scene,uv-direction/6.0).rgb+texture(scene,uv+direction/6.0).rgb);
            vec3 outer=.5*inner+.25*(texture(scene,uv-direction*.5).rgb+texture(scene,uv+direction*.5).rgb);
            float y=luma(outer);result=(y<low || y>high)?inner:outer;
        }
    }
    if(occlusion>0)result*=1.-ambientOcclusion(uv);
    if(bloom>0){
        vec3 glow=vec3(0);float total=0.;
        // Fixed, bounded screen-space glow; no motion history or blur of the scene.
        int directions=bloomQuality==0?4:bloomQuality==1?8:16;
        for(int i=0;i<16;i++){
            if(i>=directions)break;
            float a=float(i)*6.2831853/float(directions);vec2 d=vec2(cos(a),sin(a))*texel;
            glow+=bright(texture(scene,uv+d*3.0).rgb)*.65;
            glow+=bright(texture(scene,uv+d*8.0).rgb)*.35;
            if(bloomQuality==2){glow+=bright(texture(scene,uv+d*16.0).rgb)*.25;total+=.25;}
            total+=1.;
        }
        result+=glow/total*float(bloom)*.10;
    }
    if(hdr!=0){
        // Existing scene shading is display-oriented. Float storage preserves
        // highlights above 1; a soft shoulder maps them onto an SDR display.
        vec3 x=max(result*exposure,vec3(0));
        result=(x*(2.51*x+.03))/(x*(2.43*x+.59)+.14);
    }
    pixel=vec4(clamp(result,0.,1.),1);
}
)GLSL";
constexpr const char* skyFragment=R"GLSL(#version 330 core
in vec2 screen;
out vec4 pixel;
uniform vec3 forward,right,up,sky;
uniform float aspect,fov;
uniform int mode;
uniform bool dayCycle;
uniform float daylight;
uniform vec3 skySun;
uniform sampler3D geology;
vec3 hash(vec2 p){
    vec3 q=fract(vec3(p.xyx)*vec3(0.1031,0.1030,0.0973));
    q+=dot(q,q.yxz+33.33);
    return fract((q.xxy+q.yzz)*q.zyx);
}
float stars(vec2 coordinate,float frequency,float rarity){
    vec2 site=coordinate*frequency;
    vec3 attributes=hash(floor(site));
    if(attributes.z<rarity)return 0.0;
    vec2 center=0.20+0.60*attributes.xy;
    float d=length(fract(site)-center);
    float feather=clamp(max(fwidth(site.x),fwidth(site.y)),0.018,0.108);
    return (1.0-smoothstep(0.025,0.045+feather,d))*attributes.z;
}
// Seeded piecewise-linear fault instead of the smooth wavy ribbon.
float riftNoise(float y){
    float i=floor(y),f=fract(y);
    return mix(hash(vec2(i,77.)).x,hash(vec2(i+1.,77.)).x,f);
}
float riftEdge(float y){return (riftNoise(y*6.3+14.)-.5)*.36+(riftNoise(y*19.+8.)-.5)*.12+(riftNoise(y*47.+31.)-.5)*.035;}
vec3 planetCrust(vec3 p,vec3 key){
    vec3 q=p+vec3(.23,.41,.12);
    float rock=texture(geology,q*.16).r;
    float ridges=texture(geology,q*.73+vec3(.11,rock*.07,.29)).r;
    float grit=texture(geology,q*2.7).r;
    float coarse=.57*rock+.31*ridges+.12*grit;
    float relief=1.;vec3 n=normalize(p);
    for(int i=0;i<7;++i){
        vec3 h=hash(vec2(float(i)+8.,37.));
        vec3 c=normalize(vec3(h.xy*2.-1.,.38+.70*h.z));
        float radius=.078+.12*h.z;
        float d=length(p-c)/radius;
        if(d<1.3){
            float bowl=1.-smoothstep(.15,.94,d);
            float rim=1.-smoothstep(.035,.22,abs(d-1.));
            relief*=1.-.37*bowl;relief+=.20*rim;
            n=normalize(n+(c-p)*(.42*bowl/radius-.26*rim/radius));
        }
    }
    vec3 normal=normalize(n+vec3((ridges-.5)*.23,(grit-.5)*.15,0.));
    float sun=max(0.,dot(normal,key));
    vec3 stone=mix(vec3(.20,.16,.15),vec3(.64,.42,.30),smoothstep(.23,.77,coarse));
    return stone*(.07+1.10*sun)*relief+vec3(.019,.026,.042)*(1.-sun)+vec3(.22,.11,.045)*pow(1.-max(0.,n.z),4.)*sun;
}
void main(){
    vec3 ray=normalize(forward+(screen.x*aspect*right+screen.y*up)/fov);
    float h=clamp(ray.y,0.0,1.0);
    vec3 outputColor=sky;
    float day=dayCycle?daylight:(mode==1?1.0:0.0);
    vec3 solar=dayCycle?skySun:normalize(vec3(.42,.61,.67));
    float sun=max(dot(ray,solar),0.0);
    vec3 daySky=mix(sky,vec3(.13,.34,.57),sqrt(h));
    daySky+=mix(vec3(.23,.14,.03),vec3(.56,.17,.035),solarTwilight)*pow(sun,12.0);
    daySky+=vec3(1.0,.79,.44)*smoothstep(.9992,.9998,sun)*smoothstep(-.06,.04,solar.y);
    vec3 nightSky=mix(sky,vec3(.004,.011,.032),smoothstep(0.0,.80,h));
    if(ray.y>0.0 && day<.999){
        float galactic=abs(dot(ray,normalize(vec3(.56,.55,-.65))));
        float band=1.0-smoothstep(.024,.35,galactic);
        nightSky+=vec3(.046,.051,.079)*band*band;
        vec2 coordinates=vec2(atan(ray.z,ray.x)/6.283185+.5,asin(ray.y)/3.141593+.5);
        float field=stars(coordinates,270.0,.91)*.62;
        field+=stars(coordinates+vec2(.13,-.04),850.0,.960-band*.021)*(.19+.35*band);
        nightSky+=vec3(.76,.87,1.0)*field*smoothstep(0.0,.14,h);
        vec3 moon=dayCycle?-solar:vec3(-.474,.449,-.757);
        float proximity=dot(ray,moon);
        float disk=smoothstep(.99910,.99922,proximity);
        float shade=smoothstep(-.009,.006,dot(ray-moon,vec3(-.44,.22,.74)));
        nightSky+=vec3(.12,.16,.24)*pow(max(proximity,0.0),100.0);
        nightSky=mix(nightSky,vec3(.64,.69,.75)*(.28+.72*shade),disk);
    }
    outputColor=mix(nightSky,daySky,day);
    // Independent intersections with two displaced pieces of a sphere.
    // The missing wedge reveals sky and the inclined fault has real depth.
    const vec3 planet=vec3(0.4833,0.4214,-0.7674);
    float front=dot(ray,planet);
    if(front>.70 && ray.y>0.){
        vec3 side=normalize(cross(vec3(0,1,0),planet));
        vec3 vertical=cross(planet,side);
        vec2 p=vec2(dot(ray,side),dot(ray,vertical))/.405;
        vec3 rockColor=vec3(0.);float closest=-10.;
        const vec3 key=vec3(.81,.44,.38);
        for(int plate=0;plate<2;++plate){
            float signPlate=plate==0?-1.:1.;
            vec3 offset=plate==0?vec3(-.13,.065,.025):vec3(.16,-.095,-.16);
            vec2 q=p-offset.xy;
            float r2=dot(q,q);
            if(r2>=1.)continue;
            float extent=sqrt(1.-r2);
            float boundary=riftEdge(q.y);
            float gap=.085+.055*riftNoise(q.y*4.8+9.);
            float cut=(boundary+signPlate*gap-q.x-.74*q.y)/.52;
            float lo=-extent,hi=extent;
            if(plate==0)hi=min(hi,cut);else lo=max(lo,cut);
            if(hi<=lo || hi+offset.z<=closest)continue;
            vec3 hit=vec3(q,hi);
            bool inner=plate==0 && cut<extent;
            if(inner){
                float slope=(riftEdge(q.y+.002)-riftEdge(q.y-.002))/.004;
                vec3 normal=normalize(vec3(1.,.74-slope,.52));
                float direct=max(0.,dot(normal,key));
                float depth=max(0.,1.-length(hit));
                vec3 strata=hit*vec3(4.1,8.2,3.6);
                float coarse=texture(geology,strata*.28+vec3(.17,.39,.53)).r;
                float grit=texture(geology,strata*.95).r;
                float seam=1.-smoothstep(.022,.070,abs(texture(geology,strata*.66).r-.5));
                float molten=exp(-depth*13.)*(.24+.76*smoothstep(.32,.77,coarse));
                float lava=molten*(.25+.60*seam)+.44*exp(-depth*62.);
                rockColor=mix(vec3(.022,.032,.047),vec3(.16,.12,.09),coarse)*(.32+.30*direct)*(.65+.55*grit);
                rockColor+=vec3(1.,.19,.022)*lava*1.25+vec3(.85,.46,.08)*pow(lava,2.)*.72;
            }else{
                rockColor=planetCrust(hit,key);
                float fault=abs(q.x+.74*q.y+.52*hi-boundary)-gap;
                float lip=exp(-max(0.,fault)*64.);
                float vein=(1.-smoothstep(.012,.045,abs(texture(geology,hit*2.6).r-.5)))*exp(-max(0.,fault)*7.);
                rockColor+=vec3(1.,.23,.035)*(lip*.76+vein*.24);
            }
            closest=hi+offset.z;
        }
        // Depth-ordered convex debris, not tiled diamonds on the sky.
        for(int i=0;i<16;++i){
            vec3 h=hash(vec2(float(i)+14.,72.));
            float direction=i<9?-1.:1.;
            float y=direction*(.38+.99*h.x);
            vec2 center=vec2(-.74*y+riftEdge(y)+(.35+float(i%3)*.12)*(h.y-.5)*2.,y);
            float size=i<4?.075+.055*h.z:.021+.045*h.z;
            if(i<2){center=vec2(i==0?-.94:1.12,i==0?.86:-.28);size=.105+.04*h.z;}
            if(i==2){center=vec2(1.29,.39);size=.066;}
            if(i==3){center=vec2(1.46,.06);size=.047;}
            if(i==4){center=vec2(1.18,-.55);size=.055;}
            if(i==5){center=vec2(1.41,-.38);size=.030;}
            vec2 d=(p-center)/size;
            if(dot(d,d)>2.3)continue;
            float angle=h.y*6.2831853;
            mat2 turn=mat2(cos(angle),sin(angle),-sin(angle),cos(angle));
            vec2 a=turn*d;
            float low=-1.2,high=1.2;vec3 n=vec3(0,0,1);
            for(int j=0;j<10;++j){
                float az=float(j)*2.39996323+h.z*2.;
                float nz=j<5?.43+.11*float(j%3):-.47-.09*float(j%3);
                vec3 plane=vec3(cos(az),sin(az),nz);
                float reach=.72+.20*hash(vec2(float(i),float(j))).x;
                float z=(reach-dot(plane.xy,a))/nz;
                if(nz>0.){if(z<high){high=z;n=normalize(plane);}}
                else low=max(low,z);
            }
            float z=high*size+.23+.22*h.x;
            if(low<high && z>closest){
                vec3 normal=normalize(vec3(transpose(turn)*n.xy,n.z));
                float lit=max(0.,dot(normal,key));
                float grit=texture(geology,vec3(a,high)*.35+h).r;
                float coarse=texture(geology,vec3(a,high)*.13+h*.7).r;
                rockColor=mix(vec3(.041,.046,.059),vec3(.49,.31,.21),lit)*(.54+.30*grit+.37*coarse);
                if(i<4)rockColor+=vec3(.52,.10,.012)*pow(max(0.,-normal.y),4.);
                closest=z;
            }
        }
        if(closest>-9.)outputColor=mix(outputColor,rockColor,mix(.99,.76,day));
    }
    if(canopyActive)outputColor+=canopyAir(ray,45.0);
    pixel=vec4(outputColor,1.0);
}
)GLSL";
constexpr const char* hudVertex=R"GLSL(#version 330 core
layout(location=0) in vec2 pos;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 shade;
layout(location=3) in float font;
uniform vec2 size;
out vec2 tex;
out vec4 tint;
out float glyph;
void main(){ gl_Position=vec4(pos.x/size.x*2-1,1-pos.y/size.y*2,0,1); tex=uv;tint=shade;glyph=font; }
)GLSL";
constexpr const char* hudFragment=R"GLSL(#version 330 core
in vec2 tex;
in vec4 tint;
in float glyph;
uniform sampler2D atlas,menuAtlas;
out vec4 pixel;
float noiseAt(vec2 p){
    return fract(sin(dot(p,vec2(42.13,117.73)))*43758.53);
}
float cloud(vec2 p){
    vec2 tile=floor(p),f=fract(p);
    vec2 curve=f*f*(3.0-2.0*f);
    return mix(mix(noiseAt(tile),noiseAt(tile+vec2(1,0)),curve.x),
        mix(noiseAt(tile+vec2(0,1)),noiseAt(tile+1),curve.x),curve.y);
}
void main(){
    if(glyph>2.5){
        vec2 edge=min(tex,1.0-tex);
        float border=smoothstep(0.005,0.12,min(edge.x,edge.y));
        float fibers=cloud(tex*vec2(250,770))*0.025;
        float tone=0.77+border*0.12+cloud(tex*7)*0.075+cloud(tex*19)*0.035+fibers;
        pixel=vec4(tint.rgb*tone,tint.a);
        return;
    }
    float coverage=glyph>1.5?texture(menuAtlas,tex).r:glyph>0.5?texture(atlas,tex).r:1.0;
    pixel=vec4(tint.rgb,tint.a*coverage);
}
)GLSL";

constexpr std::array<std::array<int,3>,TextureCount> palette={{
    {110,120,120},{93,147,66},{116,83,55},{117,126,128},{152,99,64},{135,113,94},{53,60,65},
    {78,102,112},{139,97,53},{73,116,58},{160,109,85},{120,104,78},{168,150,100},{160,134,189},
    {48,117,150},{68,72,76},{155,154,147},{153,93,64},{198,105,59},{124,150,153},{204,116,56},
    {122,152,148},{137,82,94},{94,116,108},{190,110,51},{255,201,93},{109,153,153},
    {96,229,193},{114,52,42},{91,117,126},{216,119,61},{145,172,181},
    {228,186,161},{177,167,159},{57,58,69},{209,151,79},{105,219,224},{165,99,77}
}};
constexpr int TextureSize=256;
float rnd(int x,int y,int seed) {
    return float(mix(std::uint32_t(x)*0x9e3779b9u ^ std::uint32_t(y)*0x7f4a7c15u ^ std::uint32_t(seed))&65535)/65535;
}
float noise(float x,float y,int grid,int seed) {
    float gx=x*grid/TextureSize,gy=y*grid/TextureSize;
    int i=int(gx),j=int(gy); float a=gx-i,b=gy-j;
    a=a*a*(3-2*a);b=b*b*(3-2*b);
    auto get=[&](int X,int Y){return rnd(X%grid,Y%grid,seed);};
    return std::lerp(std::lerp(get(i,j),get(i+1,j),a),std::lerp(get(i,j+1),get(i+1,j+1),a),b);
}
std::vector<B> textures() {
    std::vector<B> data;
    data.reserve(TextureCount*TextureSize*TextureSize*3);
    for(int id=0;id<TextureCount;id++)for(int y=0;y<TextureSize;y++)for(int x=0;x<TextureSize;x++) {
        auto base=palette[id];
        const float fleck=rnd(x,y,id), fine=noise(x,y,18,id*8), waves=noise(x,y,4,id+41);
        const int px=x/2,py=y/2;
        const float pores=noise(x,y,46,id*17+110);
        float shade=0.77f+waves*0.23f+fine*0.11f+pores*0.12f+fleck*0.09f;
        if(id==SkinTexture)shade=0.93f+0.04f*fine+0.025f*waves+0.015f*pores;
        if(id==ClothTexture)shade=0.82f+fine*0.13f+0.055f*std::cos((px+py)*Pi/2)+0.045f*std::cos((px-py)*Pi/2);
        if(id==LeatherTexture)shade=0.70f+pores*0.12f+fine*0.14f+waves*0.09f;
        if(id==BrassTexture)shade=0.76f+fine*0.08f+0.15f*std::sin((py+0.8f*waves)*Pi/16);
        if(id==FluxTexture)shade=0.84f+fine*0.12f+0.18f*std::sin((px+py*0.5f)*Pi/16);
        if(id==RoofingTexture){
            const int course=x*3/TextureSize;
            const int tile=int(py+course*11)%43;
            const double seam=std::sin((px%43)*Pi/43);
            shade=0.74f+0.21f*seam+0.085f*fine+0.045f*pores;
            if(tile<2)shade*=0.72f;
            if(px%43<3)shade*=0.80f;
        }
        if(id==Grass || id==Leaves) {
            shade+=0.25f*(noise(x,y,22,13)-0.45f);
            if(fine>0.68f && fleck>0.5f) {base[0]+=40;base[1]+=24;}
        }
        if(id==CampTile){
            shade*=0.80f+0.18f*fine+0.12f*std::sin((py+waves*3)*float(Pi)/10);
            if(py%32<2)shade*=0.65f;
        }
        if(id==Timber) {
            shade+=0.16f*std::sin((px+waves*4)*Pi/4)-0.09f*std::sin((px+fine)*Pi/2);
        }
        if(id==Stone || id==Quartz || id==IronOre || id==CopperOre || id==Coal || id==Lodestone) {
            const float seam=std::abs(noise(x,y,6,id*11)-0.48f);
            if (seam<0.035f) shade*=0.62f;
            if (id==CopperOre && fine>0.51f && waves>0.47f) base={106,164,139};
            if (id==IronOre && fine>0.56f) base={177,109,72};
            if (id==Quartz && fine>0.48f) base={205,186,226};
            if (fleck>0.975f) shade*=1.32f;
        }
        if(id==Dirt || id==Sand || id==Clay) {
            if(fine>0.67f && fleck>0.44f) shade*=0.76f;
            if(fleck>0.947f) shade*=1.28f;
        }
        if(id==Timber || id==Quartz || id==CopperOre || id==IronOre || id==Grass || id==Leaves){
            const float grain=noise(x,y,31,id*61+18);
            shade*=0.94f+0.13f*grain;
            if((id==Quartz || id==CopperOre || id==IronOre) && fleck>0.97f)
                shade*=1.12f;
        }
        if(id==CampTile) {
            if(px%64<3 || py%64<3) shade*=0.56f;
            if(((px/64)^(py/64))!=0) shade*=0.87f;
        }
        if(id>=Copper && id!=Enamel && id!=SampleCopper && id!=SampleIron) {
            shade=0.81f+waves*0.15f+fleck*0.10f+0.08f*std::sin(px*Pi/2);
        }
        for(int i:base) data.push_back(B(std::clamp(int(i*shade),0,255)));
    }
    return data;
}
}

void Geometry::triangle(Vec a,Vec b,Vec c,int material,Color shade,float repeat) {
    const Vec crossProduct=cross(b-a,c-a);
    const double area=length(crossProduct);
    if(area==0)return;
    const Vec n=crossProduct/area;
    int axis=0;
    if(std::abs(n.y)>std::abs(n.x))axis=1;
    if(std::abs(n.z)>std::abs(n[axis]))axis=2;
    const int u=(axis+1)%3,v=(axis+2)%3;
    const double phaseU=std::floor(a[u]*repeat),phaseV=std::floor(a[v]*repeat);
    for(Vec p:{a,b,c}) points.push_back({float(p.x),float(p.y),float(p.z),
        float(n.x),float(n.y),float(n.z),float(p[u]*repeat-phaseU),float(p[v]*repeat-phaseV),
        float(material),shade.r,shade.g,shade.b});
}
void Geometry::quad(Vec a,Vec b,Vec c,Vec d,int m,Color shade,float repeat) {
    triangle(a,b,c,m,shade,repeat);triangle(a,c,d,m,shade,repeat);
}
void Geometry::face(Vec o,double size,int side,int material,Color shade) {
    int axis=side/2,a=(axis+1)%3,b=(axis+2)%3;
    Vec p=o,q,r,s;
    if(side%2) p[axis]+=size;
    q=r=s=p; q[a]+=size; r[a]+=size; r[b]+=size; s[b]+=size;
    if(side%2) quad(p,q,r,s,material,shade,float(1/size));
    else quad(p,s,r,q,material,shade,float(1/size));
}
void Geometry::box(Vec lo,Vec hi,int material,Color shade) {
    for(int side=0;side<6;side++) {
        int axis=side/2,a=(axis+1)%3,b=(axis+2)%3;
        Vec p=lo,q,r,s;
        if(side%2)p[axis]=hi[axis];
        q=r=s=p; q[a]=hi[a];r[a]=hi[a];r[b]=hi[b];s[b]=hi[b];
        if(side%2) quad(p,q,r,s,material,shade,2);
        else quad(p,s,r,q,material,shade,2);
    }
}
void Geometry::tube(Vec from,Vec to,double radius,int material,Color shade,int sides) {
    const Vec dir=unit(to-from);
    if(length(dir)<0.5) return;
    const Vec x=unit(cross(dir,std::abs(dir.y)<0.85?Vec{0,1,0}:Vec{1,0,0}));
    const Vec y=cross(dir,x);
    for(int i=0;i<sides;i++) {
        const double a=2*Pi*i/sides,b=2*Pi*(i+1)/sides;
        const Vec p=(x*std::cos(a)+y*std::sin(a))*radius,q=(x*std::cos(b)+y*std::sin(b))*radius;
        quad(from+p,from+q,to+q,to+p,material,shade,float(0.8/radius));
        triangle(from,from+q,from+p,material,shade,float(0.8/radius));
        triangle(to,to+p,to+q,material,shade,float(0.8/radius));
    }
}
void Geometry::stone(Vec position,double radius,int material,Color shade,int rings,int sides) {
    for(int y=0;y<rings;y++) for(int x=0;x<sides;x++) {
        auto p=[&](int a,int b){
            double lat=Pi*a/rings, lon=2*Pi*b/sides;
            return position+Vec{std::sin(lat)*std::cos(lon),std::cos(lat),std::sin(lat)*std::sin(lon)}*radius;
        };
        quad(p(y,x),p(y+1,x),p(y+1,x+1),p(y,x+1),material,shade,float(0.7/radius));
    }
}
Mesh::Mesh(Mesh&& other) noexcept:vao(std::exchange(other.vao,0)),buffer(std::exchange(other.buffer,0)),indexBuffer(std::exchange(other.indexBuffer,0)),vertices(other.vertices),terrainSurface(other.terrainSurface),twoSidedLeaf(other.twoSidedLeaf) {}
Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if(this!=&other){ clear();vao=std::exchange(other.vao,0);buffer=std::exchange(other.buffer,0);indexBuffer=std::exchange(other.indexBuffer,0);vertices=other.vertices;terrainSurface=other.terrainSurface;twoSidedLeaf=other.twoSidedLeaf; }
    return *this;
}
Mesh::~Mesh(){clear();}
void Mesh::clear() {
    if(indexBuffer)glDeleteBuffers(1,&indexBuffer);
    if(buffer)glDeleteBuffers(1,&buffer);
    if(vao)glDeleteVertexArrays(1,&vao);
    indexBuffer=buffer=vao=0;vertices=0;
}
void Mesh::uploadTerrain(Geometry& data){
    // Clipped shovel patches and adjoining materials have separate triangles.
    // Average the shading at coincident positions while keeping the actual
    // intersection planes used by mining and collision unchanged.
    struct NormalSum {Vec sum;};
    std::unordered_map<Cell,NormalSum,CellHash> normals;
    const auto key=[](const Vertex& v){
        return Cell{int(std::lround(v.x*1048576.0)),int(std::lround(v.y*1048576.0)),
                    int(std::lround(v.z*1048576.0))};
    };
    for(std::size_t i=0;i<data.points.size();i+=3){
        const auto& a=data.points[i];const auto& b=data.points[i+1];const auto& c=data.points[i+2];
        const Vec face=cross(Vec{b.x-a.x,b.y-a.y,b.z-a.z},Vec{c.x-a.x,c.y-a.y,c.z-a.z});
        for(int j=0;j<3;j++)normals[key(data.points[i+j])].sum+=face;
    }
    for(auto& v:data.points){
        const Vec face{v.nx,v.ny,v.nz};
        const Vec average=unit(normals[key(v)].sum);
        if(dot(average,face)<=0)continue;
        const auto normal=unit(average*0.86+face*0.14);
        v.nx=normal.x;v.ny=normal.y;v.nz=normal.z;
    }
    upload(data);
    terrainSurface=true;
}
void Mesh::upload(const Geometry& data,bool stream) {
    terrainSurface=false;twoSidedLeaf=false;
    if(indexBuffer){glDeleteBuffers(1,&indexBuffer);indexBuffer=0;}
    if(data.points.empty()){clear();return;}
    const bool fresh=!vao;
    if(fresh){glGenVertexArrays(1,&vao);glGenBuffers(1,&buffer);}
    vertices=int(data.points.size());
    glBindVertexArray(vao);glBindBuffer(ArrayBuffer,buffer);
    glBufferData(ArrayBuffer,data.points.size()*sizeof(Vertex),data.points.data(),stream?Stream:Static);
    if(!fresh)return;
    attribute(0,3,sizeof(Vertex),0);attribute(1,3,sizeof(Vertex),3*sizeof(float));
    attribute(2,2,sizeof(Vertex),6*sizeof(float));attribute(3,1,sizeof(Vertex),8*sizeof(float));attribute(4,3,sizeof(Vertex),9*sizeof(float));
}

void Mesh::uploadIndexed(const Geometry& data,const std::vector<unsigned>& indices){
    terrainSurface=false;twoSidedLeaf=false;
    if(data.points.empty() || indices.empty()){clear();return;}
    const bool fresh=!vao;
    if(fresh){glGenVertexArrays(1,&vao);glGenBuffers(1,&buffer);}
    glBindVertexArray(vao);glBindBuffer(ArrayBuffer,buffer);
    glBufferData(ArrayBuffer,data.points.size()*sizeof(Vertex),data.points.data(),Stream);
    // The rig's topology is immutable. Only positions/normals change per pose.
    if(!indexBuffer || vertices!=int(indices.size())){
        if(!indexBuffer)glGenBuffers(1,&indexBuffer);
        glBindBuffer(0x8893,indexBuffer);
        glBufferData(0x8893,indices.size()*sizeof(unsigned),indices.data(),Static);
    }
    vertices=int(indices.size());
    if(fresh){attribute(0,3,sizeof(Vertex),0);attribute(1,3,sizeof(Vertex),3*sizeof(float));
        attribute(2,2,sizeof(Vertex),6*sizeof(float));attribute(3,1,sizeof(Vertex),8*sizeof(float));attribute(4,3,sizeof(Vertex),9*sizeof(float));}
}

namespace {
constexpr std::array<int GraphicsOptions::*,GraphicsOptions::Count> options={
    &GraphicsOptions::resolution,&GraphicsOptions::aa,&GraphicsOptions::bloom,
    &GraphicsOptions::shadows,&GraphicsOptions::rays,&GraphicsOptions::distance,
    &GraphicsOptions::detail,&GraphicsOptions::foliage,&GraphicsOptions::vsync,&GraphicsOptions::fps,
    &GraphicsOptions::occlusion,&GraphicsOptions::shadowRange,&GraphicsOptions::anisotropy,
    &GraphicsOptions::shadowFilter,&GraphicsOptions::hdr,&GraphicsOptions::exposure,&GraphicsOptions::bloomThreshold,&GraphicsOptions::bloomQuality};
constexpr std::array<int,GraphicsOptions::Count> low={25,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},high={200,5,3,5,4,3,3,3,1,4,4,3,4,3,1,4,4,2};
constexpr std::array<const char*,4> quality={"Off","Low","Standard","High"};
}
int GraphicsOptions::value(int row) const{return this->*options.at(row);}
void GraphicsOptions::adjust(int row,int direction){
    if(row<0 || row>=Count)return;
    auto& v=this->*options[row];v=std::clamp(v+direction*(row==0?10:1),low[row],high[row]);
}
std::string GraphicsOptions::label(int row) const{
    const int v=value(row);
    switch(row){
        case 0:return std::to_string(v)+"%";
        case 1:return std::array<const char*,6>{"Off","FXAA","2x MSAA","4x MSAA","8x MSAA","4x + FXAA"}[v];
        case 2:case 6:case 7:return quality[v];
        case 10:return v==4?"Ultra":quality[v];
        case 3:return std::array<const char*,6>{"Off","384","768","1536","2048","4096"}[v];
        case 4:return v==4?"Ultra":quality[v];
        case 5:return std::array<const char*,4>{"Near","Reduced","Standard","Far"}[v];
        case 8:return v?"On":"Off";
        case 9:return std::array<const char*,5>{"Unlimited","30","45","60","120"}[v];
        case 11:return std::array<const char*,4>{"24 m","40 m","58 m","80 m"}[v];
        case 12:return std::array<const char*,5>{"Off","2x","4x","8x","16x"}[v];
        case 13:return std::array<const char*,4>{"Single","4 tap","9 tap","25 tap"}[v];
        case 14:return v?"Filmic SDR":"Off";
        case 15:return std::array<const char*,5>{"0.65x","0.8x","1.0x","1.25x","1.5x"}[v];
        case 16:return std::array<const char*,5>{"0.40","0.53","0.66","0.80","1.00"}[v];
        case 17:return std::array<const char*,3>{"Low","Standard","High"}[v];
    }
    return {};
}
void GraphicsOptions::preset(int level){
    *this=GraphicsOptions{};
    if(level==0){resolution=70;shadows=1;rays=0;distance=1;detail=1;foliage=1;fps=1;shadowRange=1;shadowFilter=0;}
    if(level==2){aa=1;bloom=1;occlusion=1;shadows=3;rays=2;detail=3;anisotropy=3;shadowFilter=2;fps=3;}
    if(level==3){resolution=100;aa=3;bloom=2;occlusion=4;shadows=5;rays=4;distance=3;detail=3;foliage=3;shadowRange=3;anisotropy=4;shadowFilter=3;hdr=1;bloomQuality=2;fps=3;}
}
void GraphicsOptions::load(const std::filesystem::path& file){
    std::ifstream input(file);std::string magic;int version=0;
    if(!(input>>magic>>version) || magic!="SCALE_GRAPHICS" || (version!=1 && version!=2))return;
    GraphicsOptions candidate;
    for(int i=0;i<(version==1?12:Count);i++){
        int v;if(!(input>>v))return;
        candidate.*options[i]=std::clamp(v,low[i],high[i]);
    }
    *this=candidate;
}
void GraphicsOptions::save(const std::filesystem::path& file) const{
    if(file.empty())return;
    std::error_code error;std::filesystem::create_directories(file.parent_path(),error);
    if(error)return;
    auto temp=file;temp+=".tmp";
    std::ofstream out(temp,std::ios::trunc);
    out<<"SCALE_GRAPHICS 2\n";for(int i=0;i<Count;i++)out<<value(i)<<' ';out<<'\n';out.close();
    if(out){std::filesystem::rename(temp,file,error);if(error)std::filesystem::remove(temp,error);}
}

Renderer::Renderer(const std::filesystem::path& assets) {
    loadGL();
    glGetIntegerv(0x8d57,&maxSamples); // GL_MAX_SAMPLES
    int extensions=0;glGetIntegerv(0x821d,&extensions);
    for(int i=0;i<extensions;++i){
        const auto* ext=glGetStringi(0x1f03,unsigned(i));
        if(ext && (std::string_view(reinterpret_cast<const char*>(ext))=="GL_EXT_texture_filter_anisotropic" ||
            std::string_view(reinterpret_cast<const char*>(ext))=="GL_ARB_texture_filter_anisotropic")){
            glGetFloatv(0x84ff,&maxAnisotropy);break;
        }
    }
    for(int index=0;index<int(worldPrograms.size());index++){
        std::string fragment=withCanopy(fieldFragment);
        std::string options;
        if(index&1)options+="#define CLIPPED\n";
        if(index&2)options+="#define LOG_DEPTH\n";
        fragment.insert(fragment.find('\n')+1,options);
        worldPrograms[index]=program(fieldVertex,fragment.c_str());
    }
    hudProgram=program(hudVertex,hudFragment);
    const auto skySource=withCanopy(skyFragment);
    skyProgram=program(skyVertex,skySource.c_str());
    postProgram=program(skyVertex,postFragment);
    canopyProgram=program(canopyVertex,canopyFragment);
    glGenTextures(1,&canopyDepth);glBindTexture(Texture,canopyDepth);
    glTexImage2D(Texture,0,0x81a6,shadowSize,shadowSize,0,0x1902,0x1405,nullptr);
    glTexParameteri(Texture,FilterMin,Linear);glTexParameteri(Texture,FilterMag,Linear);
    glTexParameteri(Texture,0x884c,0x884e); // compare reference to shadow depth
    glTexParameteri(Texture,0x884d,0x0203); // LEQUAL
    glTexParameteri(Texture,WrapS,Edge);glTexParameteri(Texture,WrapT,Edge);
    glGenFramebuffers(1,&canopyTarget);glBindFramebuffer(Framebuffer,canopyTarget);
    glFramebufferTexture2D(Framebuffer,DepthAttachment,Texture,canopyDepth,0);
    glDrawBuffer(0);glReadBuffer(0);
    canopyAvailable=glCheckFramebufferStatus(Framebuffer)==Complete;
    glBindFramebuffer(Framebuffer,0);
    glGenTextures(1,&creatureDepth);glBindTexture(Texture,creatureDepth);
    glTexImage2D(Texture,0,0x81a6,creatureSize,creatureSize,0,0x1902,0x1405,nullptr);
    glTexParameteri(Texture,FilterMin,Linear);glTexParameteri(Texture,FilterMag,Linear);
    glTexParameteri(Texture,0x884c,0x884e);glTexParameteri(Texture,0x884d,0x0203);
    glTexParameteri(Texture,WrapS,Edge);glTexParameteri(Texture,WrapT,Edge);
    glGenFramebuffers(1,&creatureTarget);glBindFramebuffer(Framebuffer,creatureTarget);
    glFramebufferTexture2D(Framebuffer,DepthAttachment,Texture,creatureDepth,0);glDrawBuffer(0);glReadBuffer(0);
    creatureAvailable=glCheckFramebufferStatus(Framebuffer)==Complete;
    glBindFramebuffer(Framebuffer,0);
    glPixelStorei(0x0cf5,1);
    glGenTextures(1,&atlas);glBindTexture(Layers,atlas);
    const auto maps=textures();
    glTexImage3D(Layers,0,0x8051,TextureSize,TextureSize,TextureCount,0,RGB,Byte,maps.data());
    glTexParameteri(Layers,FilterMin,0x2703); glTexParameteri(Layers,FilterMag,Linear); glGenerateMipmap(Layers);
    constexpr unsigned volumeTexture=0x806f;
    std::vector<B> noise(64*64*64);
    for(unsigned i=0;i<noise.size();i++)noise[i]=B(mix(i+3419u)>>24);
    glGenTextures(1,&geology);glBindTexture(volumeTexture,geology);
    glTexImage3D(volumeTexture,0,0x8229,64,64,64,0,Red,Byte,noise.data());
    glTexParameteri(volumeTexture,FilterMin,0x2703);glTexParameteri(volumeTexture,FilterMag,Linear);
    glGenerateMipmap(volumeTexture);
    std::array<B,256*180> glyphs{};
    std::ifstream source(assets/"font.gray",std::ios::binary);
    if(!source.read(reinterpret_cast<char*>(glyphs.data()),glyphs.size())) throw std::runtime_error("Missing game font atlas.");
    glGenTextures(1,&font);glBindTexture(Texture,font);
    glTexImage2D(Texture,0,0x8229,256,180,0,Red,Byte,glyphs.data());
    glTexParameteri(Texture,FilterMin,Linear);glTexParameteri(Texture,FilterMag,Linear);
    glTexParameteri(Texture,WrapS,Edge);glTexParameteri(Texture,WrapT,Edge);
    std::array<B,384*204> menuGlyphs{};
    std::ifstream menuSource(assets/"menu.gray",std::ios::binary);
    if(!menuSource.read(reinterpret_cast<char*>(menuGlyphs.data()),menuGlyphs.size()))
        throw std::runtime_error("Missing menu font atlas.");
    glGenTextures(1,&menuFont);glBindTexture(Texture,menuFont);
    glTexImage2D(Texture,0,0x8229,384,204,0,Red,Byte,menuGlyphs.data());
    glTexParameteri(Texture,FilterMin,Linear);glTexParameteri(Texture,FilterMag,Linear);
    glTexParameteri(Texture,WrapS,Edge);glTexParameteri(Texture,WrapT,Edge);
    glGenVertexArrays(1,&hudVao);glGenBuffers(1,&hudBuffer);
    glBindVertexArray(hudVao);glBindBuffer(ArrayBuffer,hudBuffer);
    attribute(0,2,sizeof(HudVertex),0);attribute(1,2,sizeof(HudVertex),2*sizeof(float));
    attribute(2,4,sizeof(HudVertex),4*sizeof(float));attribute(3,1,sizeof(HudVertex),8*sizeof(float));
    glBlendFunc(0x0302,0x0303);

    // Submit each terrain shader family before interactive zoom first asks
    // for it. Some drivers defer compilation until the first actual draw.
    Geometry geometry;
    geometry.box({-0.25,-0.25,-2},{0.25,0.25,-1.6},Stone);
    Mesh warmup;
    warmup.upload(geometry);
    const Cut cut{{1,1,1},{2,2,2},0.005};
    View view{{},{0,0,-1},{1,0,0},{0,1,0}};
    for(int resolution:{100,55}){
        renderScale(resolution);
        for(double near:{0.03,0.000002}){
            view.near=near;
            view.far=90;
            start(view,32,32);
            for(bool cull:{false,true}){
                faceCulling(cull);
                draw(warmup);
                draw(warmup,{},1,0,&cut);
            }
        }
    }
    faceCulling(false);
    renderScale(100);
    glBindFramebuffer(Framebuffer,0);
    settle();
}
Renderer::~Renderer() {
    glDeleteTextures(1,&geology);
    glDeleteTextures(1,&canopyDepth);glDeleteFramebuffers(1,&canopyTarget);
    glDeleteTextures(1,&creatureDepth);glDeleteFramebuffers(1,&creatureTarget);
    uniformCache.erase(canopyProgram);glDeleteProgram(canopyProgram);
    if(northAtlas)glDeleteTextures(1,&northAtlas);
    if(msTarget)glDeleteFramebuffers(1,&msTarget);
    if(msColor)glDeleteRenderbuffers(1,&msColor);
    if(msDepth)glDeleteRenderbuffers(1,&msDepth);
    if(sceneTarget)glDeleteFramebuffers(1,&sceneTarget);
    if(sceneColor)glDeleteTextures(1,&sceneColor);
    uniformCache.erase(postProgram);glDeleteProgram(postProgram);
    if(sceneDepth)glDeleteTextures(1,&sceneDepth);
    for(auto fence:frameFences)if(fence)glDeleteSync(fence);
    glDeleteBuffers(1,&hudBuffer);glDeleteVertexArrays(1,&hudVao);
    glDeleteTextures(1,&menuFont);glDeleteTextures(1,&font);glDeleteTextures(1,&atlas);
    for(auto shader:worldPrograms){
        uniformCache.erase(shader);
        glDeleteProgram(shader);
    }
    uniformCache.erase(hudProgram);
    glDeleteProgram(hudProgram);
    uniformCache.erase(skyProgram);
    glDeleteProgram(skyProgram);
}
bool View::sees(Box bounds,double aspect,double distance) const{
    const Vec half=(bounds.hi-bounds.lo)*0.5;
    const Vec offset=(bounds.lo+bounds.hi)*0.5-camera;
    const auto support=[&](Vec n){return std::abs(n.x)*half.x+std::abs(n.y)*half.y+std::abs(n.z)*half.z;};
    if(dot(offset,forward)+support(forward)<near || dot(offset,forward)-support(forward)>distance)return false;
    const auto h=forward*(aspect/fov),v=forward/fov;
    for(auto plane:{h+right,h-right,v+up,v-up})
        if(dot(offset,plane)+support(plane)<0)return false;
    return true;
}

std::string Renderer::graphicsLabel(int row) const{
    const auto label=graphics_.label(row);
    if(row==1 && graphics_.aa>=2){
        const int desired=graphics_.aa==2?2:graphics_.aa==4?8:4;
        const int actual=activeSamples();
        if(actual<desired)return actual>1?std::to_string(actual)+"x (limited)":"FXAA fallback";
    }
    if(row==12 && graphics_.anisotropy>0){
        if(maxAnisotropy<=1)return "Unsupported";
        if(float(1<<graphics_.anisotropy)>maxAnisotropy)return std::to_string(int(maxAnisotropy))+"x (limited)";
    }
    return label;
}

void Renderer::beginScene(){
    const int width=std::max(1,width_*graphics_.resolution/100);
    const int height=std::max(1,height_*graphics_.resolution/100);
    scaledScene=graphics_.resolution!=100 || graphics_.aa || graphics_.bloom || graphics_.occlusion || graphics_.hdr;
    msActive=false;
    if(!scaledScene){
        glBindFramebuffer(Framebuffer,0);
        glViewport(0,0,width_,height_);
        return;
    }
    if(!sceneTarget){
        glGenFramebuffers(1,&sceneTarget);
        glGenTextures(1,&sceneColor);
        glGenTextures(1,&sceneDepth);
    }
    glBindFramebuffer(Framebuffer,sceneTarget);
    if(width!=sceneWidth || height!=sceneHeight || sceneHDR!=graphics_.hdr){
        glActiveTexture(0x84c5);glBindTexture(Texture,sceneColor);
        glTexImage2D(Texture,0,graphics_.hdr?0x881a:0x8058,width,height,0,0x1908,graphics_.hdr?Float:Byte,nullptr);
        glTexParameteri(Texture,FilterMin,Linear);glTexParameteri(Texture,FilterMag,Linear);
        glTexParameteri(Texture,WrapS,Edge);glTexParameteri(Texture,WrapT,Edge);
        glFramebufferTexture2D(Framebuffer,ColorAttachment,Texture,sceneColor,0);
        glBindTexture(Texture,0);glActiveTexture(0x84c0);
        glActiveTexture(0x84c6);glBindTexture(Texture,sceneDepth);
        glTexImage2D(Texture,0,0x81a6,width,height,0,0x1902,0x1405,nullptr);
        glTexParameteri(Texture,FilterMin,0x2600);glTexParameteri(Texture,FilterMag,0x2600);
        glTexParameteri(Texture,WrapS,Edge);glTexParameteri(Texture,WrapT,Edge);
        glFramebufferTexture2D(Framebuffer,DepthAttachment,Texture,sceneDepth,0);
        glBindTexture(Texture,0);glActiveTexture(0x84c0);
        if(glCheckFramebufferStatus(Framebuffer)!=Complete){
            graphics_.resolution=100;graphics_.aa=graphics_.bloom=graphics_.occlusion=graphics_.hdr=0;
            beginScene();
            return;
        }
        sceneWidth=width;sceneHeight=height;sceneHDR=graphics_.hdr;
    }
    const int wanted=graphics_.aa==2?2:graphics_.aa==3 || graphics_.aa==5?4:graphics_.aa==4?8:1;
    int samples=1;for(int n:{2,4,8})if(n<=wanted && n<=maxSamples)samples=n;
    // Avoid enormous multisample buffers at high supersampling factors.
    // Supersampling remains independently available at full resolution.
    while(samples>1 && std::int64_t(width)*height*samples>40'000'000)samples/=2;
    if(samples>1){
        if(!msTarget){glGenFramebuffers(1,&msTarget);glGenRenderbuffers(1,&msColor);glGenRenderbuffers(1,&msDepth);}
        glBindFramebuffer(Framebuffer,msTarget);
        if(msWidth!=width || msHeight!=height || msSamples!=samples || msHDR!=graphics_.hdr){
            glBindRenderbuffer(Renderbuffer,msColor);
            glRenderbufferStorageMultisample(Renderbuffer,samples,graphics_.hdr?0x881a:0x8058,width,height);
            glFramebufferRenderbuffer(Framebuffer,ColorAttachment,Renderbuffer,msColor);
            glBindRenderbuffer(Renderbuffer,msDepth);
            glRenderbufferStorageMultisample(Renderbuffer,samples,0x81a6,width,height);
            glFramebufferRenderbuffer(Framebuffer,DepthAttachment,Renderbuffer,msDepth);
            glBindRenderbuffer(Renderbuffer,0);
            msWidth=width;msHeight=height;msSamples=samples;msHDR=graphics_.hdr;
        }
        msActive=glCheckFramebufferStatus(Framebuffer)==Complete;
        if(!msActive)glBindFramebuffer(Framebuffer,sceneTarget);
    }
    glViewport(0,0,width,height);
}

void Renderer::start(const View& v,int width,int height,const std::array<Light,8>& lights) {
    auto& fence=frameFences[frameIndex];
    if(fence){
        constexpr U waitExpired=0x911b,waitFailed=0x911d,flushCommands=1;
        const U result=glClientWaitSync(fence,flushCommands,30'000'000);
        if(result==waitExpired || result==waitFailed)glFinish();
        glDeleteSync(fence);
        fence=nullptr;
    }
    view=v;width_=width;height_=height;hud.clear();
    canopyActive=false;creatureActive=false;
    beginScene();glClearColor(v.sky.r,v.sky.g,v.sky.b,1);
    glClear(0x4000|0x0100);glEnable(Depth);glDisable(Blend);
    glActiveTexture(0x84c3);glBindTexture(0x806f,geology);
    glActiveTexture(0x84c2);glBindTexture(Texture,northAtlas?northAtlas:font);
    glActiveTexture(0x84c0);glBindTexture(Layers,atlas);
    if(maxAnisotropy>1 && appliedAnisotropy!=graphics_.anisotropy){
        glTexParameterf(Layers,0x84fe,std::min(maxAnisotropy,float(1<<graphics_.anisotropy)));
        appliedAnisotropy=graphics_.anisotropy;
    }
    activeField=0;
    fieldState={};
    fieldLights=lights;
    viewFog=v.fog;
    logarithmicDepth=v.far/v.near>2e6;
}

bool Renderer::beginCanopy(std::uint64_t revision){
    if(!canopyAvailable || !view.forestLighting || graphics_.shadows==0)return false;
    const int size=std::array<int,6>{0,384,768,1536,2048,4096}[graphics_.shadows];
    const double radius=std::array<double,4>{24.,40.,58.,80.}[graphics_.shadowRange];
    if(radius!=shadowRadius){shadowRadius=radius;canopyValid=false;}
    if(size!=shadowSize){
        glActiveTexture(0x84c4);glBindTexture(Texture,canopyDepth);
        glTexImage2D(Texture,0,0x81a6,size,size,0,0x1902,0x1405,nullptr);
        glBindTexture(Texture,0);glActiveTexture(0x84c0);
        shadowSize=size;canopyValid=false;
    }
    const Vec center{std::floor(view.camera.x/8.0)*8.0,32.0,std::floor(view.camera.z/8.0)*8.0};
    canopyActive=true;
    if(canopyValid && center==canopyCenter && revision==canopyRevision && view.shadowSun==canopySunDirection)return false;
    canopyCenter=center;canopyRevision=revision;canopySunDirection=view.shadowSun;
    glActiveTexture(0x84c4);glBindTexture(Texture,0);
    glBindFramebuffer(Framebuffer,canopyTarget);glViewport(0,0,shadowSize,shadowSize);
    glEnable(Depth);glDepthFunc(0x0201);glDepthMask(1);glDisable(CullFace);
    glClear(0x0100);glUseProgram(canopyProgram);
    const Vec right=shadowRight(canopySunDirection);
    uvec(canopyProgram,"sunDir",canopySunDirection);uvec(canopyProgram,"sunRight",right);
    uvec(canopyProgram,"sunUp",cross(canopySunDirection,right));
    glUniform1f(uni(canopyProgram,"shadowRadius"),shadowRadius);
    glUniform1f(uni(canopyProgram,"shadowDepthRange"),100.f);
    glUniform1f(uni(canopyProgram,"modelScale"),1);
    glUniform2f(uni(canopyProgram,"modelYaw"),1,0);glUniform2f(uni(canopyProgram,"modelPitch"),1,0);
    return true;
}
bool Renderer::canopyIntersects(Box bounds) const{
    // Conservative AABB projection onto the three shadow-clip axes. This
    // drops only casters that cannot touch the actual cached shadow volume.
    const Vec center=(bounds.lo+bounds.hi)*.5-canopyCenter;
    const Vec half=(bounds.hi-bounds.lo)*.5;
    const Vec right=shadowRight(canopySunDirection),up=cross(canopySunDirection,right);
    const auto overlaps=[&](Vec axis,double limit){
        const double extent=std::abs(axis.x)*half.x+std::abs(axis.y)*half.y+std::abs(axis.z)*half.z;
        return std::abs(dot(center,axis))<=limit+extent;
    };
    return overlaps(right,shadowRadius) && overlaps(up,shadowRadius) && overlaps(canopySunDirection,100.0);
}
void Renderer::shadowTree(const Mesh& mesh,Vec origin){
    if(!mesh.vertices)return;
    uvec(canopyProgram,"offset",origin-canopyCenter);
    glBindVertexArray(mesh.vao);
    if(mesh.indexBuffer)glDrawElements(Triangles,mesh.vertices,0x1405,nullptr);
    else glDrawArrays(Triangles,0,mesh.vertices);
}
void Renderer::endCanopy(){
    canopyValid=true;
    beginScene();
    glActiveTexture(0x84c4);glBindTexture(Texture,canopyDepth);
    glActiveTexture(0x84c0);
    activeField=0;fieldState={};
}
bool Renderer::beginCreatureShadow(Vec center,double radius){
    if(!creatureAvailable || !canopyActive || graphics_.shadows<2)return false;
    creatureCenter=center;creatureRadius=std::max(radius,.001);
    creatureDepthRange=std::max(creatureRadius*3.,.003);
    const int size=graphics_.shadows>=4?2048:1024;
    glActiveTexture(0x84c7);glBindTexture(Texture,creatureDepth);
    if(size!=creatureSize){glTexImage2D(Texture,0,0x81a6,size,size,0,0x1902,0x1405,nullptr);creatureSize=size;}
    glBindTexture(Texture,0);glActiveTexture(0x84c0);
    glBindFramebuffer(Framebuffer,creatureTarget);glViewport(0,0,size,size);
    glEnable(Depth);glDepthFunc(0x0201);glDepthMask(1);glDisable(CullFace);glClear(0x0100);glUseProgram(canopyProgram);
    const Vec right=shadowRight(canopySunDirection);
    uvec(canopyProgram,"sunDir",canopySunDirection);uvec(canopyProgram,"sunRight",right);uvec(canopyProgram,"sunUp",cross(canopySunDirection,right));
    glUniform1f(uni(canopyProgram,"shadowRadius"),creatureRadius);
    glUniform1f(uni(canopyProgram,"shadowDepthRange"),creatureDepthRange);
    creatureActive=true;return true;
}
void Renderer::shadowCreature(const Mesh& mesh,Vec origin,double scale,double heading,double pitch){
    if(!mesh.vertices)return;
    uvec(canopyProgram,"offset",origin-creatureCenter);
    glUniform1f(uni(canopyProgram,"modelScale"),scale);
    glUniform2f(uni(canopyProgram,"modelYaw"),cos(heading),sin(heading));
    glUniform2f(uni(canopyProgram,"modelPitch"),cos(pitch),sin(pitch));
    glBindVertexArray(mesh.vao);
    if(mesh.indexBuffer)glDrawElements(Triangles,mesh.vertices,0x1405,nullptr);
    else glDrawArrays(Triangles,0,mesh.vertices);
}
void Renderer::endCreatureShadow(){
    beginScene();glActiveTexture(0x84c7);glBindTexture(Texture,creatureDepth);glActiveTexture(0x84c0);
    activeField=0;fieldState={};
}
void Renderer::configureCanopy(unsigned shader){
    glUniform1i(uni(shader,"canopyActive"),canopyActive);
    glUniform1i(uni(shader,"canopyMap"),4);
    glUniform1i(uni(shader,"creatureMap"),7);
    glUniform1i(uni(shader,"creatureActive"),creatureActive);
    uvec(shader,"creatureOffset",view.camera-creatureCenter);
    glUniform1f(uni(shader,"creatureRadius"),creatureRadius);
    glUniform1f(uni(shader,"creatureDepthRange"),creatureDepthRange);
    glUniform1i(uni(shader,"shadowFilter"),graphics_.shadowFilter);
    glUniform1i(uni(shader,"canopySteps"),graphics_.rays==0?0:graphics_.rays==1?4:graphics_.rays==4?32:graphics_.rays==3?20:graphics_.resolution<80?6:10);
    glActiveTexture(0x84c4);glBindTexture(Texture,canopyDepth);
    glActiveTexture(0x84c7);glBindTexture(Texture,creatureDepth);glActiveTexture(0x84c0);
    uvec(shader,"shadowOffset",view.camera-canopyCenter);
    const Vec direction=canopyActive?canopySunDirection:view.shadowSun;
    const Vec right=shadowRight(direction);
    uvec(shader,"sunDir",direction);uvec(shader,"sunRight",right);uvec(shader,"sunUp",cross(direction,right));
    glUniform1f(uni(shader,"shadowWeight"),view.shadowWeight);
    glUniform1f(uni(shader,"solarTwilight"),view.twilight);
    glUniform1f(uni(shader,"shadowRadius"),shadowRadius);
}

void Renderer::setNorthTexture(){
    if(northAtlas)return;
    constexpr int width=4096;
    std::vector<B> texture(std::size_t(width)*width);
    for(int y=0;y<width;y++){
        const float warp=14*std::sin(y*0.019)+9*std::sin(y*0.034);
        for(int x=0;x<width;x++){
            const auto pore=mix(std::uint32_t(x)*97169u^std::uint32_t(y)*150919u);
            const auto grain=mix(std::uint32_t(x/7)*34759u^std::uint32_t(y/6)*84493u);
            const auto ridge=(x+int(warp))&63;
            const int flecks=static_cast<int>(pore&31);
            const int skin=151+static_cast<int>(grain&45)+flecks-(ridge<2?23:0)-(pore%997<4?69:0);
            texture[std::size_t(y)*width+x]=static_cast<B>(skin);
        }
    }
    glGenTextures(1,&northAtlas);
    glActiveTexture(0x84c2);
    glBindTexture(Texture,northAtlas);
    glTexImage2D(Texture,0,0x8229,width,width,0,Red,Byte,texture.data());
    glTexParameteri(Texture,FilterMin,0x2703);
    glTexParameteri(Texture,FilterMag,Linear);
    glGenerateMipmap(Texture);
    glActiveTexture(0x84c0);
}

void Renderer::drawSky(){
    if(!view.skyMode)return;
    glDepthMask(0);
    glDepthFunc(0x0203);
    glUseProgram(skyProgram);
    glBindVertexArray(hudVao);
    configureCanopy(skyProgram);
    glUniform1i(uni(skyProgram,"mode"),view.skyMode);
    glUniform1i(uni(skyProgram,"geology"),3);
    glUniform1i(uni(skyProgram,"dayCycle"),view.dayCycle);
    glUniform1f(uni(skyProgram,"daylight"),view.daylight);
    uvec(skyProgram,"skySun",view.sun);
    glUniform1f(uni(skyProgram,"aspect"),double(width_)/height_);
    glUniform1f(uni(skyProgram,"fov"),view.fov);
    uvec(skyProgram,"forward",view.forward);
    uvec(skyProgram,"right",view.right);
    uvec(skyProgram,"up",view.up);
    uvec(skyProgram,"sky",{view.sky.r,view.sky.g,view.sky.b});
    glDrawArrays(Triangles,0,3);
    glDepthFunc(0x0201);
    glDepthMask(1);
    activeField=0;
}

void Renderer::configureField(unsigned shader){
    configureCanopy(shader);
    uvec(shader,"fieldSun",(view.dayCycle || view.forestLighting)?view.sun:unit({.42,.86,.27}));
    glUniform1i(uni(shader,"materials"),0);
    glUniform1i(uni(shader,"northSkin"),2);
    glUniform1i(uni(shader,"geology"),3);
    const double body=std::clamp(view.detailBody,1e-12,1.0);
    const double zoom=std::log(1.0/body)/std::log(4.0);
    const double octave=std::floor(zoom),fraction=zoom-octave;
    const double frequency=.90*std::pow(4.0,octave);
    const auto phase=[&](double f,double yScale){
        Vec p{view.camera.x*f,view.camera.y*f*yScale,view.camera.z*f};
        for(int i=0;i<3;++i)p[i]-=std::floor(p[i]);
        return p;
    };
    uvec(shader,"detailPhase0",phase(frequency,1));
    uvec(shader,"detailPhase1",phase(frequency*4,1));
    uvec(shader,"woodPhase0",phase(frequency,.125));
    uvec(shader,"woodPhase1",phase(frequency*4,.125));
    glUniform1f(uni(shader,"detailFrequency"),frequency);
    glUniform1f(uni(shader,"detailBlend"),fraction*fraction*(3-2*fraction));
    glUniform1f(uni(shader,"detailAmount"),std::min(1.0,zoom)*std::array<float,4>{0.f,.55f,1.f,1.25f}[graphics_.detail]);
    glUniform1f(uni(shader,"detailInvBody"),1.0/body);
    glUniform1i(uni(shader,"surfaceQuality"),graphics_.detail);
    glUniform1f(uni(shader,"aspect"),float(width_)/height_);
    glUniform1f(uni(shader,"fov"),view.fov);
    glUniform1f(uni(shader,"zNear"),view.near);
    glUniform1f(uni(shader,"zFar"),view.far);
    if(logarithmicDepth){
        glUniform1f(uni(shader,"depthNear"),view.near);
        glUniform1f(uni(shader,"depthNormalization"),1/std::log2(view.far/view.near));
    }
    glUniform1f(uni(shader,"ambient"),view.ambient);
    uvec(shader,"forward",view.forward);
    uvec(shader,"beamDirection",view.forward);
    glUniform1f(uni(shader,"beamReach"),view.torchRange);
    uvec(shader,"right",view.right);
    uvec(shader,"up",view.up);
    uvec(shader,"sky",{view.sky.r,view.sky.g,view.sky.b});
    F lights[32]{};
    int count=0;
    for(const auto& lamp:fieldLights){
        if(lamp.strength<=0)continue;
        const Vec offset=lamp.position-view.camera;
        lights[count*4]=offset.x;lights[count*4+1]=offset.y;
        lights[count*4+2]=offset.z;lights[count*4+3]=lamp.trail?-lamp.strength:lamp.strength;
        ++count;
    }
    glUniform1f(uni(shader,"trailRadius"),float(natural::TrailLightRadius));
    glUniform1i(uni(shader,"lightCount"),count);
    glUniform1f(uni(shader,"effectTime"),float(std::fmod(view.effectTime,1200.)));
    glUniform1i(uni(shader,"gateLightCount"),std::clamp(view.gateLightCount,0,2));
    for(int i=0;i<std::clamp(view.gateLightCount,0,2);i++)uvec(shader,i?"gateLights[1]":"gateLights[0]",view.gateLights[i]-view.camera);
    if(count)glUniform4fv(uni(shader,"lamps"),count,lights);
}

void Renderer::draw(const Mesh& mesh,Vec origin,double scale,double angle,const Cut* cut,float emissive,
                    float cellGrid,float lodBias,bool insideCut,double pitch,double gait){
    if(!mesh.vertices)return;
    const int index=(logarithmicDepth?2:0)+(cut?1:0);
    const unsigned shader=worldPrograms[index];
    auto& state=fieldState[index];
    if(activeField!=shader){
        glUseProgram(shader);
        activeField=shader;
    }
    if(!state.configured){
        configureField(shader);
        state.configured=true;
    }
    const bool fresh=!state.hasPose;
    if(fresh || state.fog!=viewFog){glUniform1f(uni(shader,"fog"),viewFog);state.fog=viewFog;}
    if(fresh || state.origin!=origin){
        const Vec relative=origin-view.camera;
        const Vec high{float(relative.x),float(relative.y),float(relative.z)};
        // Recover the sub-micrometer camera position after subtracting from
        // a chunk vertex whose local coordinate may be several meters away.
        uvec(shader,"origin",high);
        uvec(shader,"originRemainder",relative-high);
        uvec(shader,"terrainOrigin",origin);
        state.origin=origin;
    }
    if(fresh || state.terrain!=mesh.terrainSurface){
        glUniform1i(uni(shader,"terrainSurface"),mesh.terrainSurface);state.terrain=mesh.terrainSurface;
    }
    if(fresh || state.twoSidedLeaf!=mesh.twoSidedLeaf){
        glUniform1i(uni(shader,"twoSidedLeaf"),mesh.twoSidedLeaf);state.twoSidedLeaf=mesh.twoSidedLeaf;
    }
    if(fresh || state.scale!=scale){glUniform1f(uni(shader,"scale"),scale);state.scale=scale;}
    if(fresh || state.angle!=angle){
        glUniform2f(uni(shader,"rotation"),std::cos(angle),std::sin(angle));state.angle=angle;
    }
    if(fresh || state.pitch!=pitch){
        glUniform2f(uni(shader,"tilt"),std::cos(pitch),std::sin(pitch));state.pitch=pitch;
    }
    if(fresh || state.gait!=gait){glUniform1f(uni(shader,"gait"),gait);state.gait=gait;}
    if(fresh || state.emissive!=emissive){glUniform1f(uni(shader,"emissive"),emissive);state.emissive=emissive;}
    if(fresh || state.grid!=cellGrid){glUniform1f(uni(shader,"cellGrid"),cellGrid);state.grid=cellGrid;}
    if(fresh || state.bias!=lodBias){glUniform1f(uni(shader,"lodBias"),lodBias);state.bias=lodBias;}
    if(cut){
        const int mode=insideCut?2:1;
        if(fresh || state.cutMode!=mode){glUniform1i(uni(shader,"cut"),mode);state.cutMode=mode;}
        if(fresh || state.cut.lo!=cut->lo || state.cut.hi!=cut->hi || state.cut.ownerInset!=cut->ownerInset){
            uvec(shader,"cutLo",cut->lo-view.camera);uvec(shader,"cutHi",cut->hi-view.camera);
            glUniform1f(uni(shader,"cutOwnerInset"),cut->ownerInset);
            state.cut=*cut;
        }
    }
    state.hasPose=true;
    glBindVertexArray(mesh.vao);
    if(mesh.indexBuffer)glDrawElements(Triangles,mesh.vertices,0x1405,nullptr);
    else glDrawArrays(Triangles,0,mesh.vertices);
}
void Renderer::glyph(float x,float y,float w,float h,int character,Color tint,bool menu) {
    const int c=std::clamp(character-32,0,95);
    const float u=float(c%16)/16,v=float(c/16)/6;
    const float u2=u+1.f/16,v2=v+1.f/6;
    auto add=[&](float X,float Y,float Uv,float Vv){
        hud.push_back({X,Y,Uv,Vv,tint.r,tint.g,tint.b,tint.a,menu?2.0f:float(character!=0)});
    };
    add(x,y,u,v);add(x+w,y,u2,v);add(x+w,y+h,u2,v2);
    add(x,y,u,v);add(x+w,y+h,u2,v2);add(x,y+h,u,v2);
}
void Renderer::rect(float x,float y,float w,float h,Color color){glyph(x,y,w,h,0,color);}
void Renderer::translucent(bool enabled){
    if(enabled){glEnable(Blend);glBlendFunc(0x0302,0x0303);glDepthMask(0);}
    else {glDepthMask(1);glDisable(Blend);}
}
void Renderer::overlay(bool enabled){if(enabled)glDisable(Depth);else glEnable(Depth);}
void Renderer::faceCulling(bool enabled){if(enabled)glEnable(CullFace);else glDisable(CullFace);}
void Renderer::distanceFog(double distance){viewFog=distance;}
void Renderer::vellum(float x,float y,float w,float h,Color color){
    const auto add=[&](float X,float Y,float u,float v){
        hud.push_back({x+X*w,y+Y*h,u,v,color.r,color.g,color.b,color.a,3});
    };
    add(0,0,0,0);add(1,0,1,0);add(1,1,1,1);
    add(0,0,0,0);add(1,1,1,1);add(0,1,0,1);
}
void Renderer::diamond(float x,float y,float w,float h,Color color){
    const auto add=[&](float px,float py){hud.push_back({px,py,0,0,color.r,color.g,color.b,color.a,0});};
    add(x,y-h);add(x+w,y);add(x,y+h);
    add(x,y-h);add(x,y+h);add(x-w,y);
}
void Renderer::line(float x,float y,float w,float h,Color color){rect(x,y,w,h,color);}
void Renderer::text(float x,float y,std::string_view value,Color tint,float scale) {
    for (unsigned char letter:value) {
        if(letter!=' ')glyph(x,y,16*scale,30*scale,letter,tint);
        x+=13*scale;
    }
}
void Renderer::title(float x,float y,std::string_view value,Color tint,float scale){
    for(unsigned char letter:value){
        const float advance=(letter==' '?9:std::string_view("!iIl.,:;'").find(letter)!=std::string_view::npos?8:
            std::string_view("MWmw").find(letter)!=std::string_view::npos?23:
            letter>='A' && letter<='Z'?17:14)*scale;
        if(letter!=' '){
            const float left=x+advance/2-12*scale;
            glyph(left+1,y+1,24*scale,34*scale,letter,{0.09f,0.045f,0.02f},true);
            glyph(left,y,24*scale,34*scale,letter,tint,true);
        }
        x+=advance;
    }
}
void Renderer::finishHud() {
    if(scaledScene){
        if(msActive){
            glBindFramebuffer(ReadFramebuffer,msTarget);glBindFramebuffer(DrawFramebuffer,sceneTarget);
            glBlitFramebuffer(0,0,sceneWidth,sceneHeight,0,0,sceneWidth,sceneHeight,0x4000|0x0100,0x2600);
        }
        if(graphics_.aa || graphics_.bloom || graphics_.occlusion || graphics_.hdr || graphics_.resolution>100){
            glBindFramebuffer(Framebuffer,0);glViewport(0,0,width_,height_);
            glDisable(Depth);glDisable(Blend);glDisable(CullFace);glUseProgram(postProgram);
            glActiveTexture(0x84c5);glBindTexture(Texture,sceneColor);
            glUniform1i(uni(postProgram,"scene"),5);
            glActiveTexture(0x84c6);glBindTexture(Texture,sceneDepth);
            glUniform1i(uni(postProgram,"sceneDepth"),6);
            glUniform1i(uni(postProgram,"occlusion"),graphics_.occlusion);
            glUniform1i(uni(postProgram,"logDepth"),logarithmicDepth);
            glUniform1f(uni(postProgram,"nearPlane"),view.near);glUniform1f(uni(postProgram,"farPlane"),view.far);
            glUniform1f(uni(postProgram,"aspect"),double(width_)/height_);glUniform1f(uni(postProgram,"fov"),view.fov);
            glUniform2f(uni(postProgram,"texel"),1.f/sceneWidth,1.f/sceneHeight);
            glUniform2f(uni(postProgram,"outputTexel"),1.f/width_,1.f/height_);
            glUniform1i(uni(postProgram,"hdr"),graphics_.hdr);
            glUniform1i(uni(postProgram,"bloomQuality"),graphics_.bloomQuality);
            glUniform1f(uni(postProgram,"exposure"),std::array<float,5>{.65f,.8f,1.f,1.25f,1.5f}[graphics_.exposure]);
            glUniform1f(uni(postProgram,"bloomThreshold"),std::array<float,5>{.40f,.53f,.66f,.80f,1.f}[graphics_.bloomThreshold]);
            glUniform1i(uni(postProgram,"aa"),!msActive && graphics_.aa>=2?1:graphics_.aa);glUniform1i(uni(postProgram,"bloom"),graphics_.bloom);
            glBindVertexArray(hudVao);glDrawArrays(Triangles,0,3);
            glBindTexture(Texture,0);glActiveTexture(0x84c5);glBindTexture(Texture,0);glActiveTexture(0x84c0);
        }else{
            glBindFramebuffer(ReadFramebuffer,sceneTarget);
            glBindFramebuffer(DrawFramebuffer,0);
            glBlitFramebuffer(0,0,sceneWidth,sceneHeight,0,0,width_,height_,0x4000,Linear);
            glBindFramebuffer(Framebuffer,0);glViewport(0,0,width_,height_);
        }
    }
    glDisable(Depth);glEnable(Blend);glUseProgram(hudProgram);
    glUniform2f(uni(hudProgram,"size"),width_,height_);
    glActiveTexture(0x84c0);glBindTexture(Texture,font);glUniform1i(uni(hudProgram,"atlas"),0);
    glActiveTexture(0x84c1);glBindTexture(Texture,menuFont);glUniform1i(uni(hudProgram,"menuAtlas"),1);
    glBindVertexArray(hudVao);glBindBuffer(ArrayBuffer,hudBuffer);
    const auto bytes=hud.size()*sizeof(HudVertex);
    if(bytes>hudCapacity){
        hudCapacity=std::max<std::size_t>(65536,hudCapacity*2);
        while(hudCapacity<bytes)hudCapacity*=2;
        glBufferData(ArrayBuffer,hudCapacity,nullptr,Stream);
    }
    if(bytes)glBufferSubData(ArrayBuffer,0,bytes,hud.data());
    glDrawArrays(Triangles,0,int(hud.size()));
    frameFences[frameIndex]=glFenceSync(0x9117,0);
    if(!frameFences[frameIndex])glFinish();
    frameIndex=(frameIndex+1)%frameFences.size();
}
void Renderer::settle(){glFinish();}
void Renderer::screenshot(const std::filesystem::path& file) const {
    std::vector<B> pixels(std::size_t(width_)*height_*3);
    glPixelStorei(0x0d05,1);glReadPixels(0,0,width_,height_,RGB,Byte,pixels.data());
    std::ofstream output(file,std::ios::binary);
    output<<"P6\n"<<width_<<" "<<height_<<"\n255\n";
    for(int row=height_-1;row>=0;row--)output.write(reinterpret_cast<char*>(pixels.data()+row*width_*3),width_*3);
}
