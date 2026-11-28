#pragma once
#include "math.hpp"
#include <filesystem>
#include <string_view>
#include <string>
#include <vector>

enum SurfaceTexture { SkinTexture=32,ClothTexture,LeatherTexture,BrassTexture,FluxTexture,RoofingTexture,TextureCount,NorthSkinTexture=TextureCount,WitnessSkinTexture,WitnessBoneTexture,WitnessToothTexture,TrailSurfaceTexture,WitnessEyeTexture,WitnessMantleTexture,RifleMetalTexture,RifleGlowTexture,RifleGripTexture,RifleLensTexture,SuitWeaveTexture,SuitArmorTexture,SuitRubberTexture,SuitLightTexture,GoblinSkinTexture,GoblinHairTexture,GoblinLeatherTexture,GoblinBronzeTexture,GoblinClothTexture,GoblinAmberTexture,GoblinEyeTexture,DeerFurTexture,FeedingBloodTexture,NorthMaskTexture,NorthMetalTexture,NorthClothTexture,NorthGlowTexture,GateStoneTexture,GateEnergyTexture,GateRuneTexture };
struct Color { float r,g,b,a=1; };
struct Vertex { float x,y,z,nx,ny,nz,u,v,id,r,g,b; };
class Geometry {
public:
    std::vector<Vertex> points;
    void triangle(Vec a,Vec b,Vec c,int id,Color shade,float repeat=1);
    void quad(Vec a,Vec b,Vec c,Vec d,int id,Color shade,float repeat=1);
    void face(Vec origin,double size,int side,int material,Color shade);
    void box(Vec lo,Vec hi,int material,Color shade={1,1,1});
    void tube(Vec from,Vec to,double radius,int material,Color shade={1,1,1},int sides=8);
    void stone(Vec position,double radius,int material,Color shade={1,1,1},int rings=5,int sides=8);
};
struct Mesh {
    unsigned vao{},buffer{},indexBuffer{};
    int vertices{};
    bool terrainSurface{},twoSidedLeaf{};
    Mesh()=default;
    Mesh(const Mesh&)=delete;
    Mesh& operator=(const Mesh&)=delete;
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;
    ~Mesh();
    void upload(const Geometry& data,bool stream=false);
    void uploadIndexed(const Geometry& data,const std::vector<unsigned>& indices);
    void uploadTerrain(Geometry& data);
    void clear();
};
struct Cut { Vec lo,hi; double ownerInset{}; };
struct View {
    Vec camera{},forward{0,0,1},right{1,0,0},up{0,1,0};
    double near=0.025,far=160,fog=75;
    Color sky{0.34f,0.55f,0.62f};
    float ambient=0.62f;
    float fov=1.0f;
    int skyMode=0;
    float torchRange=0;
    double detailBody=1.0; // local player scale, independent of render mesh tier
    bool forestLighting=false;
    bool dayCycle=false;
    Vec sun=unit({.42,.61,.67}),shadowSun=sun;
    float daylight=1,twilight=0,shadowWeight=1;
    double effectTime{};
    std::array<Vec,2> gateLights{};
    int gateLightCount{};

    bool sees(Box bounds,double aspect,double distance) const;
};
// User-selectable graphics settings, independent of saves and simulation.
struct GraphicsOptions {
    int resolution=100, aa=0, bloom=0, shadows=2, rays=2, distance=2, detail=2, foliage=2, vsync=1, fps=3, occlusion=0, shadowRange=2, anisotropy=0, shadowFilter=1, hdr=0, exposure=2, bloomThreshold=2, bloomQuality=1;
    bool operator==(const GraphicsOptions&) const=default;
    static constexpr int Count=18;
    int value(int row) const;
    void adjust(int row,int direction);
    void preset(int level);
    std::string label(int row) const;
    void load(const std::filesystem::path& file);
    void save(const std::filesystem::path& file) const;
};
struct Light { Vec position; float strength=0; bool trail=false; };
class Renderer {
    std::array<unsigned,4> worldPrograms{};
    unsigned hudProgram{},skyProgram{},atlas{},geology{},northAtlas{},font{},menuFont{},hudVao{},hudBuffer{};
    int width_{},height_{};
    unsigned sceneTarget{},sceneColor{},sceneDepth{};
    int sceneWidth{},sceneHeight{};
    unsigned msTarget{},msColor{},msDepth{};
    int msWidth{},msHeight{},msSamples{},maxSamples=1,sceneHDR=-1,msHDR=-1;
    int appliedAnisotropy=-1; float maxAnisotropy=1;
    bool msActive=false;
    GraphicsOptions graphics_{};
    unsigned postProgram{};
    int shadowSize=768;
    double shadowRadius=58.0;
    bool scaledScene=false;
    void beginScene();
    View view;
    std::array<Light,8> fieldLights{};
    double viewFog{};
    unsigned activeField{};
    struct DrawState {
        bool configured{},hasPose{},terrain{},twoSidedLeaf{};
        Vec origin;
        double angle{},pitch{},scale{},gait{},emissive{},grid{},bias{},fog{};
        int cutMode{};
        Cut cut{};
    };
    std::array<DrawState,4> fieldState{};
    bool logarithmicDepth{};
    unsigned canopyProgram{},canopyTarget{},canopyDepth{};
    unsigned creatureTarget{},creatureDepth{};
    bool creatureAvailable{},creatureActive{};
    int creatureSize=1024;
    Vec creatureCenter{};
    double creatureRadius=7,creatureDepthRange=21;
    bool canopyAvailable{},canopyValid{},canopyActive{};
    Vec canopyCenter{},canopySunDirection{};
    std::uint64_t canopyRevision{};
    void configureCanopy(unsigned shader);

    void configureField(unsigned program);
    struct HudVertex { float x,y,u,v,r,g,b,a,font; };
    std::vector<HudVertex> hud;
    std::size_t hudCapacity{};
    std::array<void*,2> frameFences{};
    std::size_t frameIndex{};
    void glyph(float x,float y,float w,float h,int character,Color tint,bool menu=false);
public:
    explicit Renderer(const std::filesystem::path& assets);
    ~Renderer();
    void start(const View& v,int width,int height,const std::array<Light,8>& lights={});
    void draw(const Mesh& mesh,Vec origin={},double scale=1,double angle=0,const Cut* cut=nullptr,float emissive=0,float cellGrid=0,float lodBias=0,bool insideCut=false,double pitch=0,double gait=0);
    void rect(float x,float y,float w,float h,Color color);
    void overlay(bool enabled);
    void translucent(bool enabled);
    void drawSky();
    // Cached directional shadow map; no additional terrain generation.
    bool beginCanopy(std::uint64_t revision);
    bool canopyIntersects(Box bounds) const;
    void shadowTree(const Mesh& mesh,Vec origin);
    bool beginCreatureShadow(Vec center,double radius);
    void shadowCreature(const Mesh& mesh,Vec origin,double scale,double heading,double pitch);
    void endCreatureShadow();
    void endCanopy();
    void setNorthTexture();
    void distanceFog(double distance);
    void faceCulling(bool enabled);
    void vellum(float x,float y,float w,float h,Color color);
    void diamond(float x,float y,float w,float h,Color color);
    void line(float x,float y,float w,float h,Color color);
    void title(float x,float y,std::string_view value,Color tint,float scale=1);
    void text(float x,float y,std::string_view text,Color tint,float scale=1);
    float textWidth(std::string_view s,float scale=1) const { return s.size()*13.0f*scale; }
    void finishHud();
    void renderScale(int percent){graphics_.resolution=std::clamp(percent,25,200);}
    int activeSamples() const{return msActive?msSamples:1;}
    std::string graphicsLabel(int row) const;
    int renderScale() const {return graphics_.resolution;}
    GraphicsOptions& graphics(){return graphics_;}
    const GraphicsOptions& graphics() const{return graphics_;}
    void settle();
    void screenshot(const std::filesystem::path& destination) const;
    int width() const { return width_; }
    int height() const { return height_; }
};
