#pragma once
#include "daylight.hpp"
#include "witness-cover.hpp"
#include "witness-horror.hpp"
#include "render.hpp"
#include "north-art.hpp"
#include "world.hpp"
#include "micro.hpp"
#include "expedition.hpp"
#include "movement.hpp"
#include "controls.hpp"
#include "settlement.hpp"
#include "goblin-rig.hpp"
#include "atmosphere.hpp"
#include <optional>
#include <string>
#include <set>

struct Motion { double ahead{},across{},rise{}; bool crouch{},jump{},brisk{},aim{}; };
struct Device {
    int id{};
    bool closed=true;
    Cell normal{0,1,0};
    int material=Stone;
    double energy{},cycleEnergy{},probeDelay{};
};
struct Electrical {
    double conductance{};
    std::unordered_map<Cell,double,CellHash> lamps,quarries,houses;
    int connected{};
};
struct GeneratorState {
    double angle{},speed{},voltage{},current{},gridWatts{},rms2{};
    double rpm=90,field=0.08;
    int turns=32;
    bool test=true;
    void advance(double dt,double load);
};
struct Chunks {
    bool proxyOnly=false;
    Mesh terrain,proxy,exact,contours,trees,leafSprigs,trailPosts,trailFlames;
    std::uint64_t version=0,treeVersion=0;
    bool treeReady=false,treeDetail=false;
    int depth=-1;
};
struct DetailMesh {
    Bounds area;
    Mesh geometry;
    std::uint64_t revision=0;
    bool valid=false;
};
class Game {
    Renderer& renderer;
    World world;
    std::filesystem::path saveFile;
    bool modernWorld{};
    double wheelCarry{},rollCooldown{};
    std::uint64_t lootState=0x045e2e74bca829d3ULL;
    int surfaceTier() const;
    int realmTier() const;
    int awardLoot(int maxCount);
    void rollLoot();
    std::array<std::int64_t,ItemLimit> bag{};
    std::unordered_map<Cell,Device,CellHash> devices;
    std::unordered_map<Cell,Chunks,CellHash> chunks;
    struct TrailCache {std::uint64_t revision{},seed{};std::vector<Vec> posts;};
    std::unordered_map<Cell,TrailCache,CellHash> trailCache;
    const std::vector<Vec>& cachedTrail(Cell tile);
    std::unordered_map<Cell,bool,CellHash> visited;
    std::array<DetailMesh,FineDepth> detail;
    Electrical network;
    GeneratorState generator;
    Mesh portal,focalPortal,gateMote;
    void drawGate(Vec origin,double scale,bool final=false);
    struct RealmTile {
        Mesh solid,close,distant,fixtures,fixtureProxy,exact,roof,roofProxy,trees,sprigs;
        Box roofBounds;
        std::uint64_t revision=~std::uint64_t{};
    };
    std::array<RealmTile,micro::TileCount> realmTiles;
    std::array<DetailMesh,FineDepth> realmDetails;
    std::array<std::array<std::unique_ptr<micro::Area>,micro::Total>,2> nests;
    int family=-1,level=-1,meshFamily=-1,meshLevel=-1;
    bool surfaceFortress{}; // final surface shrink tier uses local microscopic coordinates
    void enterSurfaceFortress();
    std::array<std::array<std::unique_ptr<expedition::Wildlife>,micro::Total>,2> fauna;
    std::array<Mesh,4> elfModels;
    struct GroveMesh {Mesh full,proxy;};
    std::array<GroveMesh,4> villageGrove;
    Mesh hostileModel,hostileProxy,villagePath,sidearm,rifle,sightModel,rifleGlow,rifleLens,roundModel,flashModel,parentScenery;
    std::array<Mesh,2> northModels;
    std::array<northart::Rig,2> northRigs;
    Geometry northPosed;
    std::array<int,2> northPoseTick{-1,-1};
    std::optional<Cell> northHome;
    Vec northFeet{};
    double northWalk{},northStride{},northStepClock{},northHeading{};
    bool northMoving{};
    void updateNorth(double dt);
    struct WitnessMesh {
        Geometry geometry;
        Mesh render;
        bool dirty=true;
        Box bounds{};
        void measure(){
            if(geometry.points.empty()){bounds={};return;}
            bounds={{1e30,1e30,1e30},{-1e30,-1e30,-1e30}};
            for(const auto& v:geometry.points){
                const Vec p{v.x,v.y,v.z};
                for(int a=0;a<3;++a){bounds.lo[a]=std::min(bounds.lo[a],p[a]);bounds.hi[a]=std::max(bounds.hi[a],p[a]);}
            }
            const Vec epsilon{1e-5,1e-5,1e-5};bounds.lo=bounds.lo-epsilon;bounds.hi+=epsilon;
        }
        void upload(bool stream=false){
            if(dirty){if(!stream)measure();render.upload(geometry,stream);dirty=false;}
        }
    };
    std::array<WitnessSkin,2> witnessSkins;
    static constexpr int GuardSites=micro::Count*2+1;
    static constexpr int LegacyGuards=GuardSites*settlement::GuardClasses;
    std::array<settlement::Guard,LegacyGuards+2*settlement::FortressGuards> guards;
    std::array<settlement::Brood,2> broods;
    Mesh fortressArchitecture,fortressFireBase,fortressFlame;
    std::array<std::array<Mesh,6>,3> broodParts;
    struct FortPose {Mesh mesh;int id=-1,lod=-1;double tick=-1;};
    std::array<FortPose,settlement::FortressGuards> fortPoses;
    bool fortressArtReady{};
    void drawFortress(const View& view);
    void updateBrood(double dt);
    void updateFortressDefenses(double dt);
    bool strikeWitness(Vec start,double reach,int damage,bool reward);
    static constexpr int WitnessMaxHealth=200;
    int witnessHealth=WitnessMaxHealth;
    void damageWitness(int amount);
    std::array<Mesh,6> goblinModels;
    std::array<Mesh,2> chestModels;
    std::array<settlement::GoblinRig,6> goblinRigs;
    std::array<std::array<Mesh,dread::Pieces>,6> guardianPieces;
    std::array<std::array<Vec,dread::Pieces>,3> guardianPieceCenter{};

    Geometry goblinPosed;
    std::array<int,6> goblinPoseSlots{-1,-1,-1,-1,-1,-1};
    std::array<double,6> goblinPoseClocks{-1,-1,-1,-1,-1,-1};
    int goblinSlot() const;
    settlement::Guard& goblin(int role=0);
    bool inFortress() const {return level==micro::Fortress && modernWorld;}
    int guardCount() const {return inFortress()?settlement::FortressGuards:level<0?settlement::GuardClasses:1;}
    int guardId(int slot) const {return inFortress()?LegacyGuards+family*settlement::FortressGuards+slot:goblinSlot()+slot*GuardSites;}
    static int guardClassForId(int id){return id>=LegacyGuards?settlement::fortressClass((id-LegacyGuards)%settlement::FortressGuards):std::clamp(id/GuardSites,0,2);}
    int guardClass(int slot) const {return guardClassForId(guardId(slot));}
    int localGuard(int id) const {return inFortress()?id-LegacyGuards-family*settlement::FortressGuards:id>=0 && id<LegacyGuards && id%GuardSites==goblinSlot()?id/GuardSites:-1;}
    bool guardHere(int id) const {const int slot=localGuard(id);return slot>=0 && slot<guardCount() && guardId(slot)==id;}

    Vec guardChest() const;
    int goblinTarget(Vec eye,Vec ray,double reach,double& hit) const;
    bool goblinStrike(settlement::Guard& guard,int role);
    void updateGoblin(double dt);
    void drawGoblin(const View& view);
    Vec goblinCorpseBase(Vec feet,double yaw,int kind,double progress);
    void drawGoblinCollapse(const settlement::Guard& guard,Vec feet,double yaw,int model,double progress);
    bool useGoblinChest();
    double goblinHit(Vec eye,Vec ray,double reach) const;
    void makeGoblinModels();
    WitnessMesh witnessBody,witnessFace,witnessJaw;
    std::array<WitnessMesh,2> witnessEyes;
    std::optional<contact::Pose> lastBodyPose;
    bool witnessProxy=false;
    struct WitnessPart {const WitnessMesh* mesh;Vec origin;double heading,tilt;};
    std::array<WitnessPart,5> witnessPose() const;
    void refreshWitnessMesh(double dt);
    double witnessCueCooldown{};
    struct Deer {Vec feet{};double heading{},stride{},idle{},fallSpeed{};};
    std::map<Cell,Deer> forestDeer;
    std::set<Cell> consumedDeer;
    double deerScan{},deerMotion{},ambientGain=1,ambientCall=8;
    int rememberedVoice=-1,lastMimicVoice=-1;
    std::bitset<6> learnedVoices;
    std::array<Mesh,17> deerModels; // idle plus 16 gait phases
    std::array<std::array<Mesh,dread::Pieces>,7> preyPieces;
    static constexpr int FallFrames=7;
    std::array<Mesh,FallFrames> fallenDeer;
    std::array<Mesh,6> fallenGoblin;
    std::array<double,6> fallenGoblinFloor{};
    std::array<std::array<Vec,dread::Pieces>,settlement::GuardClasses> fallenGoblinContacts{};
    std::array<double,FallFrames> fallenDeerFloor{};
    Geometry cleanWitnessFace,cleanWitnessJaw;
    int bloodStage=-1;
    void refreshWitnessStains();
    Mesh bloodDrop,bloodRemnant,fallingGoblin;
    Geometry fallPose;
    Mesh witnessImpact;
    void makeHorrorModels();
    void updateForestLife(double dt);
    void updateHorrorAudio(double dt);
    void drawHorror(const View& view);
    void noticeWitness(Vec point);
    std::optional<dread::Prey> findPrey(Vec head,bool goblinsOnly=false);
    Vec preyPiecePosition(int part,std::optional<Vec> bodyBase={});
    bool refreshPrey(dread::Prey& prey);
    bool consumePrey(dread::Prey& prey);
    bool horrorPlan(double dt,bool watched,bool hasLine,Vec eye,Vec head,
        const std::function<bool(Vec,double)>& fits,const std::function<Vec(Vec)>& grounded,
        const std::function<bool(Vec,Vec)>& obstructed);
    void flashLight(std::array<Light,8>& lights) const;

    struct Presence {
        Vec feet{},climbDirection{};
        stalking::SightMemory sight;
        dread::Event horror;
        Vec detour{},progressOrigin{},travelDirection{};
        double detourLeft{},climbRetry{},progressTime{},blockedTime{};
        double gazeYaw{},gazePitch{},eyeYaw{},eyePitch{},pace{},surge{},gripWait{};
        bool gazeReady{};
        double scale=1,heading{},wait{},step{};
        int family=-2,level=-2;
        double stareTime{},crestDistance{},fallSpeed{},dreadPhase{},rage{},stain{};
        Vec coverGoal{},coverLean{};
        double coverRetry{},coverTime{};
        bool seekingCover{},holdingCover{};
        // A rare, time-limited threat display. Normal locomotion stays quadrupedal.
        double uprightCooldown=12.0, uprightTime{};
        bool observed{},active{},crouched{},groundedPose{},climbing{},falling{};
        contact::Rig rig;
    } witness;
    void updateWitness(double dt);
    void drawWitness();
    void moveWitness(double dt);
    void updateWitnessRig(double dt);
    int fixtureAt(Cell cell) const;
    std::optional<Hit> witnessRay(Vec from,Vec dir,double reach);
    double witnessHit(Vec eye,Vec direction,double reach) const;
    void drawNorth(const View& view);
    Vec northOrigin(Cell cell) const;
    int prepareNorth(double range);
    int sceneryFamily=-1,sceneryLevel=-1,villageFamily=-1;
    int health=100;
    double hurtCooldown{},battery{},recoil{},walkCycle{},weaponSway{},frameSeconds{};
    Vec smoothInput{};
    int weapon=PulsePistol;
    bool armed{},sightOn{},crouched{},aiming{},portalZoom{},combatEnabled=true;
    struct Bolt { Vec from{},to{};double age=1;bool absorbed{};double scale=1;int family=-2,level=-2; };
    std::array<Bolt,12> bolts{};
    std::size_t nextBolt{};
    struct DepthPoint {Vec feet{};double yaw{},pitch{},zoom{};};
    std::array<DepthPoint,micro::Count> rope{};
    Vec innerFeet{};
    double innerZoom{},innerGoal{};
    double surfaceYaw{},surfacePitch{};
    bool flying{};
    std::array<std::optional<Vec>,2> gates={micro::Entrances[0],micro::Entrances[1]};
    enum class Workshop {Workbench,Machine,Press,House};
    Workshop workshop=Workshop::Workbench;
    std::optional<Cell> workshopCell;
    std::optional<int> gateTarget;
    double hullHeight() const {return crouched?CrouchHeight:PlayerHeight;}
    void stance(bool requested);
    bool interact(bool directKey);
    bool ownsWeapon() const;
    Vec gateAnchor(int index) const {return gates[index].value_or(micro::Entrances[index]);}
    void openWorkshop(Workshop mode,std::optional<Cell> position={});
    bool visibleRecipe(int index) const;
    double housePower(Cell position) const;
    Mesh bench,benchDistant,rotor,coil,grid,fixture,lampGlow,selectedBox,shaftMesh;
    Vec feet{0.5,29,-7.1};
    double yaw=Pi,pitch=-0.16,zoom{},goalZoom{},playTime{},fileTimer{};
    double solarClock=solarcycle::InitialClock;
    mobility::Vertical vertical;
    int precision{},lesson{},recipe{},selected{},picker{};
    bool grounded{},built{},wired{},crawled{},paused{},crafting{},materials{},electrical{},map{},night{},debug{};
    bool previousJump{},dirtyGrid=true,done{};
    int fieldWidth=1280,fieldHeight=720;
    int usedTurns=0, pickUses=96;
    double sensitivity=1.0;
    input::Bindings defaultBindings;
    input::Bindings* bindings=&defaultBindings;
    int settingsPage{},bindingRow{},graphicsRow{};
    bool bindingCapture{};
    void drawSettings(float scale);
    std::string keyName(int action) const {return bindings->name(action);}
    std::string keyHint(int action) const {return " ["+keyName(action)+"]";}
    std::string status,worldTitle;
    bool pickWorld{};
    double statusTime{};
    std::optional<Hit> target;
    Mesh slopeSelection,shovelOutline;
    Vec outlineNormal{};
    std::array<double,4> selectionHeights{};
    void drawTerrainSelection(const World& matter,Cell cell,int tier,int material,float glow);
    std::optional<Cell> deviceTarget,machineConsole;
    static constexpr Vec Rig{0.5,29,9.65};
    static constexpr Cell Port{0,29,11};
    static constexpr Cell CraftTable{-2,29,-6};
    static constexpr Cell CampStation{-1,29,13};
    double body() const;
    double timeRate() const;
    Vec camera() const;
    void message(std::string message);
    void movement(double dt,const Motion& controls);
    void recoverPosition();
    void moveMicro(double dt,const Motion& controls);
    double innerSize() const;
    int entrance() const;
    bool descend(bool shortcut);
    void ascend();
    micro::Area& layer(int set,int floor);
    void drawMicro();
    void saveMicro(std::ostream& out) const;
    expedition::Wildlife& wildlife(int set,int depth);
    void updateExpedition(double dt);
    void drawExpedition(const View& view,double aspect);
    void drawWeapon();
    void fire();
    void makeExpeditionModels();
    void makeParentScenery();
    void moveInput(double dt,Vec wish);
    bool overlapsField(Vec position,double size,double height) const;
    bool overlapsField(Vec position,double size) const {return overlapsField(position,size,hullHeight());}
    double sweepField(Vec position,double size,int axis,double distance,double height) const;
    double sweepField(Vec position,double size,int axis,double distance) const {
        return sweepField(position,size,axis,distance,hullHeight());
    }
    void focus();
    bool shovel(int material,bool deposit);
    void collect(bool exact=false);
    void place(bool exact=false);
    void toggleSwitch();
    void selectMachineMaterial(Device& machine,int material);
    void operateMachine(int key);
    bool nearCrafter() const;
    void workQuarries(double dt,double sourceEnergy);
    void workWells(double dt);
    std::optional<Cell> findDeposit(Cell machine,int material,int range) const;
    void refreshGrid();
    void makeRig();
    void makeCoil();
    void makeCircuitMesh();
    void craft();
    bool camp() const;
    bool atGenerator() const;
    void drawField();
    void drawHud();
    void drawCraft(float scale);
    void drawMaterials(float scale);
    void drawMachine(float scale) const;
    void selectMaterial(int id);
    void drawMap(float scale);
    void updateLesson();
    void terrainMesh(Chunks& mesh,Cell chunk,int depth,bool proxyOnly);
    void finerMesh(const World& matter,DetailMesh& mesh,Bounds area,int level,std::uint64_t revision);
    double lightAt(Cell location) const;
    int prepareMatterDetails(const World& matter,std::array<DetailMesh,FineDepth>& cache,
                             Vec eye,int detail,int limit,std::array<Cut,FineDepth>& cuts);
    void drawMatterDetails(const std::array<DetailMesh,FineDepth>& cache,int detail,
                          const std::array<Cut,FineDepth>& cuts);
public:
    bool flashlight{},inventory{},legacyRig=true;
    Game(Renderer& renderer,std::filesystem::path data,std::filesystem::path save,bool load=true,bool modern=false);
    ~Game();
    void look(double x,double y);
    void wheel(double amount);
    void key(int key);
    void button(int button,bool crouchOverride=false);
    void useBindings(input::Bindings& active){bindings=&active;}
    bool remapping() const {return bindingCapture;}
    bool navigating() const {return paused || inventory || crafting || materials || electrical || map || machineConsole;}
    void bindInput(int physical);
    void settingsClick(float x,float y,int width,int height);
    void menuInput(int physical,int logical);
    void renderMessage(std::string text){message(std::move(text));}
    void mouseMode(bool recenter){message(recenter?"Mouse recentering mode [F8]":"Relative mouse mode [F8]");}
    void update(double dt,const Motion& controls,double elapsed=-1);
    void draw(int width,int height);
    void frameDuration(double elapsed);
    bool save() const;
    void load();
    void readOnly(){saveFile.clear();}
    void capture(std::filesystem::path path);
    void smoke(std::filesystem::path prefix);
    void benchmark();
    void verifyModern(const std::filesystem::path& data);
    void previewWitness(const std::filesystem::path& path);
    void previewGoblin(const std::filesystem::path& path);
    void verifyFortress(const std::filesystem::path& path);
    void previewNorth(const std::filesystem::path& path);
    void verifyHorror(const std::filesystem::path& path);
    void previewRealms(const std::filesystem::path& path);
    void previewModern(const std::filesystem::path& path);
    void previewGraphics(const std::filesystem::path& path);
    void verifySession(const std::filesystem::path& data);
    bool wantsCapture() const {return !paused && !inventory && !crafting && !materials && !electrical && !map && !machineConsole && !done;}
    bool finished() const {return done;}
    void enableWorldPicker(std::string title){worldTitle=std::move(title);}
    bool requestsWorldPicker() const {return pickWorld;}
};
void modelTests(const std::filesystem::path& data);

void terrainStressTests(const std::filesystem::path& data);
