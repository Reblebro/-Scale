#include "game.hpp"
#include "controller.hpp"
#include "platform.hpp"
#include "world-picker.hpp"
#include <chrono>
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <fstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef near
#undef far
#else
#include <unistd.h>
#endif
#include <stdexcept>
#include <thread>

namespace {
std::filesystem::path executableDirectory(){
#ifdef _WIN32
    std::wstring path(32768,L'\0');
    const auto size=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    if(!size || size>=path.size())throw std::runtime_error("Could not locate the game executable.");
    path.resize(size);
    return std::filesystem::path(path).parent_path();
#else
    return std::filesystem::canonical("/proc/self/exe").parent_path();
#endif
}
struct Window {
    SDL_Window* handle{};
    SDL_GLContext context{};
    bool fullscreen=false,offscreen=false;
    explicit Window(bool windowed,bool hidden,bool capture) {
        std::vector<const char*> choices{nullptr};
#ifdef __linux__
        if(!hidden && !std::getenv("SDL_VIDEODRIVER") && std::getenv("DISPLAY") && std::getenv("WAYLAND_DISPLAY"))
            choices={"x11","wayland"};
#endif
        std::string why;
        for(const char* name:choices){
            if(name)SDL_SetHint("SDL_VIDEODRIVER",name);
            if(SDL_Init(platform::InitVideo)==0){
                SDL_GL_ResetAttributes();
                SDL_GL_SetAttribute(17,3);SDL_GL_SetAttribute(18,3);SDL_GL_SetAttribute(21,1);
                SDL_GL_SetAttribute(5,1);SDL_GL_SetAttribute(6,24);
                const auto flags=platform::OpenGL|platform::Resizable|platform::HighDpi|(hidden?platform::Hidden:0);
                handle=SDL_CreateWindow("!Scale",platform::Center,platform::Center,capture?2560:1280,capture?1440:720,flags);
                if(handle)context=SDL_GL_CreateContext(handle);
                if(context){
                    offscreen=std::string_view(SDL_GetCurrentVideoDriver())=="offscreen";
                    fullscreen=!hidden && !offscreen && !windowed && SDL_SetWindowFullscreen(handle,platform::Fullscreen)==0;
                    SDL_GL_SetSwapInterval(hidden?0:1);
                    std::cout<<"!Scale v0.7.0 using "<<SDL_GetCurrentVideoDriver()<<" graphics.\n";
                    return;
                }
            }
            if(!why.empty())why+="; ";
            why+=std::string(name?name:"SDL")+": "+SDL_GetError();
            if(handle){SDL_DestroyWindow(handle);handle=nullptr;}
            SDL_Quit();
        }
        throw std::runtime_error("Could not open OpenGL 3.3 ("+why+"). Try launching with --windowed --software.");
    }
    ~Window(){SDL_GL_DeleteContext(context);SDL_DestroyWindow(handle);SDL_Quit();}
    bool useWarp=false, preferWarp=false, relative=false;
    void center(int& x,int& y) const {
        SDL_GetWindowSize(handle,&x,&y);
        x/=2;y/=2;
    }
    void capture(bool enabled) {
        SDL_SetRelativeMouseMode(0);
        useWarp=relative=false;
        if(enabled){
            const bool canWarp=std::string_view(SDL_GetCurrentVideoDriver())!="wayland";
            if(!preferWarp || !canWarp)
                relative=SDL_SetRelativeMouseMode(1)==0 && SDL_GetRelativeMouseMode();
            useWarp=canWarp && !relative;
            if(useWarp){
                int x,y;center(x,y);SDL_WarpMouseInWindow(handle,x,y);
            }
        }
        SDL_SetWindowGrab(handle,relative || useWarp);
        int x,y;SDL_GetRelativeMouseState(&x,&y);
        SDL_ShowCursor(!(relative || useWarp));
    }
    void look(Game& game,int eventX,int eventY,bool moved) {
        int x=0,y=0;
        if(useWarp){
            if(!moved)return;
            int cx,cy;center(cx,cy);
            SDL_GetMouseState(&x,&y);x-=cx;y-=cy;
            if(x||y)SDL_WarpMouseInWindow(handle,cx,cy);
        }else if(relative){
            SDL_GetRelativeMouseState(&x,&y);
            if(!x&&!y){x=eventX;y=eventY;}
        }else{
            x=eventX;y=eventY;
        }
        if(x||y)game.look(x,y);
    }
};
void checkBundle(const std::filesystem::path& base,const std::filesystem::path& data) {
    std::error_code error;
    const auto font=base/"assets"/"font.gray";
    if(std::filesystem::file_size(font,error)!=46080 || error)
        throw std::runtime_error("Missing or invalid font at "+font.string()+
            ". Extract the complete !Scale ZIP and launch the game.");
    if(std::filesystem::file_size(base/"assets"/"menu.gray",error)!=78336 || error)
        throw std::runtime_error("Missing menu font atlas. Extract the full !Scale ZIP.");
    for(int row=-64;row<64;row++){
        auto file=data/("row-"+std::to_string(row)+".bin");
        if(std::filesystem::file_size(file,error)!=8388608 || error)
            throw std::runtime_error("Missing or incomplete terrain file "+file.string()+
                ". Extract the full ZIP, including the world folder.");
    }
}
void reportLaunchError(const std::string& error,bool interactive) {
    std::cerr<<"!Scale: "<<error<<"\n";
    if(!interactive)return;
    std::filesystem::path state;
#ifdef _WIN32
    if(const auto* local=_wgetenv(L"LOCALAPPDATA");local && *local)state=local;
#else
    auto pref=std::getenv("XDG_STATE_HOME"),home=std::getenv("HOME");
    if(pref&&*pref)state=pref;
    else if(home&&*home)state=std::filesystem::path(home)/".local/state";
#endif
    std::string message=error;
    if(!state.empty()){
        const auto folder=state/"scale-frontier";
        std::error_code failure;
        std::filesystem::create_directories(folder,failure);
        if(!failure){
            const auto file=folder/"launch-error.log";
            std::ofstream out(file);
            if(out){
                out<<"!Scale v0.7.0: "<<error<<"\n";
                message+="\n\nDetails saved to "+file.string();
            }
        }
    }
#ifdef _WIN32
    MessageBoxA(nullptr,message.c_str(),"!Scale launch error",MB_OK | MB_ICONERROR);
#else
    if(!isatty(STDERR_FILENO) && (std::getenv("DISPLAY") || std::getenv("WAYLAND_DISPLAY")))
        SDL_ShowSimpleMessageBox(0x10,"!Scale launch error",message.c_str(),nullptr);
#endif
}
void preserveOldSave(const std::filesystem::path& save) {
    if(save.empty())return;
    std::error_code error;
    if(!std::filesystem::is_regular_file(save,error) || error)return;
    std::ifstream input(save);
    std::string header;
    if(std::getline(input,header) && header=="FRONTIER 7")return;
    auto backup=save;
    backup+=".pre-0.6.0";
    if(std::filesystem::exists(backup,error) || error)return;
    if(!std::filesystem::copy_file(save,backup,std::filesystem::copy_options::none,error) && error)
        std::cerr<<"!Scale could not back up the old save: "<<error.message()<<"\n";
}
std::filesystem::path defaultSave() {
#ifdef _WIN32
    const auto* local=_wgetenv(L"LOCALAPPDATA");
    return local && *local?std::filesystem::path(local)/"scale-frontier"/"world.save":std::filesystem::path{};
#else
    const char* data=std::getenv("XDG_DATA_HOME");
    if(data && *data)return std::filesystem::path(data)/"scale-frontier"/"world.save";
    const char* home=std::getenv("HOME");
    return home?std::filesystem::path(home)/".local/share/scale-frontier/world.save":std::filesystem::path{};
#endif
}
}

int main(int argc,char* argv[]) {
#ifdef _WIN32
    SDL_SetMainReady();
#endif
    bool interactive=true;
    try {
        const auto base=executableDirectory();
        auto data=base/"world",save=defaultSave();
        std::filesystem::path smoke;
        int renderPercent=100;bool explicitRenderScale=false;
        bool windowed=false,verify=false,playTest=false,noSave=false,installation=false,startup=false,benchmark=false,explicitSave=false,witnessPreview=false,modernPreview=false,graphicsPreview=false,horrorVerify=false,goblinPreview=false,northPreview=false,realmPreview=false,fortressVerify=false;
        for(int i=1;i<argc;i++) {
            std::string_view arg=argv[i];
            if(arg=="--windowed")windowed=true;
            else if(arg=="--no-save")noSave=true;
            else if(arg=="--self-test")verify=true;
            else if(arg=="--play-test")playTest=true;
            else if(arg=="--check-install")installation=true;
            else if(arg=="--startup-test")startup=true;
            else if(arg=="--benchmark")benchmark=true;
            else if(arg=="--render-scale" && i+1<argc){
                explicitRenderScale=true;
                const std::string_view value=argv[++i];
                const auto result=std::from_chars(value.data(),value.data()+value.size(),renderPercent);
                if(result.ec!=std::errc{} || result.ptr!=value.data()+value.size() || renderPercent<25 || renderPercent>200)
                    throw std::runtime_error("Render scale must be between 25 and 200.");
            }
            else if(arg=="--software"){
#ifdef _WIN32
                throw std::runtime_error("Software mode is available on Linux. Use --windowed on Windows.");
#else
                setenv("LIBGL_ALWAYS_SOFTWARE","1",1);
#endif
            }
            else if(arg=="--world" && i+1<argc)data=argv[++i];
            else if(arg=="--save" && i+1<argc){save=argv[++i];explicitSave=true;}
            else if(arg=="--smoke" && i+1<argc)smoke=argv[++i];
            else if(arg=="--verify-fortress" && i+1<argc){smoke=argv[++i];modernPreview=fortressVerify=true;}
            else if(arg=="--verify-horror" && i+1<argc){smoke=argv[++i];modernPreview=horrorVerify=true;}
            else if(arg=="--preview-realms" && i+1<argc){smoke=argv[++i];modernPreview=realmPreview=true;}
            else if(arg=="--preview-north" && i+1<argc){smoke=argv[++i];modernPreview=northPreview=true;}
            else if(arg=="--preview-goblin" && i+1<argc){smoke=argv[++i];modernPreview=goblinPreview=true;}
            else if(arg=="--preview-witness" && i+1<argc){smoke=argv[++i];modernPreview=witnessPreview=true;}
            else if(arg=="--preview-modern" && i+1<argc){smoke=argv[++i];modernPreview=true;}
            else if(arg=="--preview-graphics" && i+1<argc){smoke=argv[++i];modernPreview=graphicsPreview=true;}
            else if(arg=="--help"){
                std::cout<<"!Scale v0.7.0\n  --windowed     Start in a window\n  --software     Use the Mesa software renderer\n  --check-install  Check all bundle files without a display\n  --no-save      Play a temporary session\n"
                    "  --world PATH   Read terrain files from PATH\n  --save PATH    Open this save directly\n"
                    "  --self-test    Verify the game model without opening a display\n"
                    "  --play-test    Exercise the tutorial, shrinking and expedition in a hidden context\n"
                    "  --benchmark    Report frame times in the surface, quarry and Elderbough\n"
                    "  --render-scale 25-200  Set 3D render resolution without scaling the HUD\n"
                    "  --preview-realms PREFIX  Render all seven hidden realms in a temporary world\n"
                    "  --preview-graphics PREFIX  Render modern trail and graphics settings test images\n"
                    "  --verify-fortress PREFIX  Test final-scale fortress, brood growth and Witness combat\n"
                    "  --verify-horror PREFIX  Test Witness encounters, rewards and persistence in a temporary world\n"
                    "  --preview-goblin PREFIX  Render the articulated goblin in a temporary world\n"
                    "  --preview-witness FILE  Render the Witness and verify sight obstruction\n";
                return 0;
            }else throw std::runtime_error("Unknown option: "+std::string(arg));
        }
        const bool hidden=!smoke.empty() || playTest || startup || benchmark;
        interactive=!hidden && !verify && !installation;
        if(verify){modelTests(data);terrainStressTests(data);worldLibraryTests();return 0;}
        checkBundle(base,data);
        if(installation){
            std::cout<<"!Scale v0.7.0 installation OK. Font and 128 terrain rows found.\n";
            return 0;
        }
        Window window(windowed,hidden,!smoke.empty() && !startup);
        Renderer renderer(base/"assets");
        loadCreatureVoices(base/"assets");
        renderer.renderScale(renderPercent);
        SDL_StopTextInput();
        if(hidden){
            Game game(renderer,data,startup&&!noSave?save:std::filesystem::path{},startup&&!noSave,modernPreview || benchmark);
            if(playTest){
                game.verifySession(data);
                worldPickerPlayTests(renderer);
                Game modern(renderer,data,{},false,true);
                modern.verifyModern(data);
            }else if(fortressVerify)game.verifyFortress(smoke);
            else if(horrorVerify)game.verifyHorror(smoke);
            else if(benchmark)game.benchmark();
            else if(startup){
                game.readOnly();
                for(int i=0;i<16;i++){game.update(1.0/60,{});game.draw(1280,720);}
                if(!smoke.empty())game.capture(smoke);
                std::cout<<"Loaded the save and rendered initial gameplay.\n";
            }else if(realmPreview)game.previewRealms(smoke);
            else if(northPreview)game.previewNorth(smoke);
            else if(goblinPreview)game.previewGoblin(smoke);
            else if(witnessPreview)game.previewWitness(smoke);
            else if(graphicsPreview)game.previewGraphics(smoke);
            else if(modernPreview)game.previewModern(smoke);
            else{
                game.smoke(smoke);
                worldPickerPlayTests(renderer,smoke);
                std::cout<<"Rendered the world picker, terrain, menus, entities and hidden worlds.\n";
            }
            return 0;
        }
        const auto sharedControls=save.parent_path()/"controls";
        const auto graphicsFile=noSave || save.empty()?std::filesystem::path{}:save.parent_path()/"graphics.txt";
        if(!graphicsFile.empty())renderer.graphics().load(graphicsFile);
        if(explicitRenderScale)renderer.renderScale(renderPercent);
        auto appliedGraphics=renderer.graphics();
        SDL_GL_SetSwapInterval(appliedGraphics.vsync);
        const bool selectWorld=!explicitSave && !noSave && !save.empty();
        if(selectWorld){
            save=save.parent_path()/"campaigns"/"0.6.0"/"world.save";
        }
        input::Bindings bindings(noSave || save.empty()?std::filesystem::path{}:sharedControls);
        std::unique_ptr<SaveLock> lock;
        std::unique_ptr<Game> game;
        std::unique_ptr<WorldPicker> picker;
        std::filesystem::path activeSave;
        const auto openWorld=[&](const std::filesystem::path& path){
            auto owned=std::make_unique<SaveLock>(path);
            preserveOldSave(path);
            auto next=std::make_unique<Game>(renderer,data,path,true,selectWorld || noSave);
            if(selectWorld){
                const auto options=WorldLibrary(save).list();
                const auto chosen=std::find_if(options.begin(),options.end(),[&](const auto& option){return option.save==path;});
                next->enableWorldPicker(chosen==options.end()?"Expedition":chosen->name);
            }
            next->useBindings(bindings);
            activeSave=path;
            lock=std::move(owned);
            game=std::move(next);
        };
        if(selectWorld)picker=std::make_unique<WorldPicker>(renderer,save);
        else openWorld(noSave?std::filesystem::path{}:save);

        Controller controller;
        bool running=true,captured=false,fullscreen=window.fullscreen;
        bool focused=(SDL_GetWindowFlags(window.handle)&0x200)!=0;
        auto last=std::chrono::steady_clock::now();
        while(running){
            SDL_Event event{};
            int motionX=0,motionY=0;
            bool mouseMoved=false;
            std::array<int,16> clicks{};
            std::size_t clickCount=0;
            const auto gameInput=[&](int code){
                using namespace platform;
                if(code==F7){
                    const int current=renderer.renderScale();
                    const int next=current>85?85:current>70?70:current>55?55:100;
                    renderer.renderScale(next);
                    const std::string label="3D resolution "+std::to_string(next)+" percent";
                    if(game)game->renderMessage(label);
                    else picker->error(label);
                }else if(code==F11){
                    const bool want=!fullscreen;
                    if(SDL_SetWindowFullscreen(window.handle,want?Fullscreen:0)==0){
                        fullscreen=want;
                        if(captured)window.capture(true);
                    }
                }else if(picker)picker->key(code);
                else if(code==F8){
                    window.preferWarp=!window.preferWarp;
                    if(captured)window.capture(true);
                    motionX=motionY=0;
                    game->mouseMode(window.useWarp);
                    mouseMoved=false;
                }else if(code>=input::Primary && code<=input::Secondary){
                    if(captured && clickCount<clicks.size())clicks[clickCount++]=code-input::FirstMouse;
                }else if(code && code<input::FirstMouse)game->key(code);
            };
            while(SDL_PollEvent(&event)){
                using namespace platform;
                switch(event.type){
                case Quit:running=false;break;
                case 0x653:case 0x654:controller.reconnect();break;
                case platform::Window:
                    if(event.window.event==12)focused=true;
                    if(event.window.event==13)focused=false;
                    break;
                case platform::Motion:
                    if(captured){motionX+=event.motion.xrel;motionY+=event.motion.yrel;mouseMoved=true;}
                    break;
                case ButtonDown:{
                    const int code=input::FirstMouse+event.button.button;
                    if(game && game->remapping())game->bindInput(code);
                    else if((picker || (game && game->navigating())) && event.button.button==1){
                        int logicalW,logicalH,w,h;
                        SDL_GetWindowSize(window.handle,&logicalW,&logicalH);
                        SDL_GL_GetDrawableSize(window.handle,&w,&h);
                        if(logicalW>0 && logicalH>0 && w>0 && h>0){
                            const float x=float(event.button.x)*w/logicalW,y=float(event.button.y)*h/logicalH;
                            if(picker)picker->click(x,y,w,h);
                            else game->settingsClick(x,y,w,h);
                        }
                    }else if(captured)gameInput(bindings.logical(code));
                    break;
                }
                case TextInput:
                    if(picker)picker->text(event.text.text);
                    break;
                case Wheel:{
                    double amount=event.wheel.preciseY?event.wheel.preciseY:event.wheel.y;
                    if(event.wheel.direction==1)amount=-amount;
                    if(picker)picker->wheel(amount);
                    else if(amount!=0){
                        const int code=amount>0?input::WheelUp:input::WheelDown;
                        if(game->remapping())game->bindInput(code);
                        else {
                            const int action=bindings.logical(code);
                            if(action==input::WheelUp || action==input::WheelDown)
                                game->wheel((action==input::WheelUp?1:-1)*std::abs(amount));
                            else gameInput(action);
                        }
                    }
                    break;
                }
                case KeyDown:{
                    const int code=event.key.keysym.scancode;
                    if(event.key.repeat){
                        if(picker && code==Backspace)picker->key(code);
                        break;
                    }
                    if(picker)gameInput(code);
                    else if(game->remapping())game->bindInput(code);
                    else if(code==platform::Escape)game->key(code);
                    else if(game->navigating()){
                        const int action=bindings.logical(code);
                        if(action==F7 || action==F8 || action==F11)gameInput(action);
                        else game->menuInput(code,action);
                    }else if(code==bindings.physical(Space) && SDL_GetKeyboardState(nullptr)[RAlt])
                        game->key(V);
                    else {
                        const int action=bindings.logical(code);
                        if(action==input::WheelUp || action==input::WheelDown)game->wheel(action==input::WheelUp?1:-1);
                        else gameInput(action);
                    }
                    break;
                }
                }
            }
            controller.sample(focused);
            using Pad=Controller;
            if(picker || (game && game->navigating())){
                const auto menuKey=[&](int key){if(picker)picker->key(key);else game->key(key);};
                if(controller.pressed(Pad::Up))menuKey(platform::Up);
                if(controller.pressed(Pad::Down))menuKey(platform::Down);
                if(controller.pressed(Pad::Left))menuKey(platform::Left);
                if(controller.pressed(Pad::Right))menuKey(platform::Right);
                if(controller.pressed(Pad::Confirm))menuKey(platform::Enter);
                if(controller.pressed(Pad::Back) || controller.pressed(Pad::Pause))menuKey(platform::Escape);
                if(game && controller.pressed(Pad::Inventory))menuKey(platform::Tab);
            }else if(game && focused){
                if(controller.pressed(Pad::Pause))game->key(platform::Escape);
                else if(controller.pressed(Pad::Inventory))game->key(platform::Tab);
                else if(controller.pressed(Pad::Map))game->key(platform::M);
                else if(captured){
                    if(controller.pressed(Pad::Grow))game->wheel(-1);
                    if(controller.pressed(Pad::Shrink))game->wheel(1);
                    if(controller.pressed(Pad::Interact))game->button(3,controller.held(Pad::Crouch));
                    if(controller.pressed(Pad::Fire))game->button(1);
                    if(controller.pressed(Pad::Down))game->key(platform::F);
                    if(controller.pressed(Pad::Up))game->key(platform::X);
                    if(controller.pressed(Pad::Left))game->key(platform::LeftBracket);
                    if(controller.pressed(Pad::Right))game->key(platform::RightBracket);
                    if(controller.pressed(Pad::Back))game->key(platform::B);
                }
            }
            if(!running || (picker && picker->finished()) || (game && game->finished()))break;
            if(picker){
                if(const auto selected=picker->selection()){
                    try{
                        openWorld(*selected);
                        picker.reset();
                    }catch(const std::exception& problem){picker->error(problem.what());}
                    motionX=motionY=0;
                    clickCount=0;
                }
            }else if(game->requestsWorldPicker()){
                game.reset();
                lock.reset();
                picker=std::make_unique<WorldPicker>(renderer,save,activeSave);
            }
            const bool next=game && focused && game->wantsCapture();
            if(next!=captured){captured=next;window.capture(captured);motionX=motionY=0;mouseMoved=false;}
            const auto* keys=SDL_GetKeyboardState(nullptr);
            const auto mouseButtons=SDL_GetMouseState(nullptr,nullptr);
            const auto held=[&](int action){return bindings.down(action,keys,mouseButtons);};
            if(captured){
                window.look(*game,motionX,motionY,mouseMoved);
                for(std::size_t i=0;i<clickCount;i++)game->button(clicks[i],held(platform::Shift));
            }
            const auto now=std::chrono::steady_clock::now();
            const double elapsed=std::chrono::duration<double>(now-last).count();
            const double dt=std::clamp(elapsed,0.001,0.075);
            last=now;
            if(game){
                game->frameDuration(elapsed);
                Motion controls;
                if(captured){
                    using namespace platform;
                    controls.ahead=int(held(W))-held(S);controls.across=int(held(D))-held(A);
                    controls.rise=int(held(Space)&&!keys[RAlt])-held(Q);
                    controls.crouch=held(Shift);controls.jump=held(Space)&&!keys[RAlt];
                    controls.brisk=held(Control);
                    controls.aim=held(input::Secondary) || controller.held(Pad::Aim);
                    controls.ahead=std::clamp(controls.ahead-controller.axis(1),-1.0,1.0);
                    controls.across=std::clamp(controls.across+controller.axis(0),-1.0,1.0);
                    controls.crouch|=controller.held(Pad::Crouch);
                    controls.brisk|=controller.held(Pad::Brisk);
                    controls.jump|=controller.held(Pad::Confirm);
                    if(controller.held(Pad::Confirm))controls.rise=1;
                    const double x=controller.axis(2),y=controller.axis(3);
                    if(x!=0 || y!=0)game->look(x*dt*740,y*dt*650);
                }
                game->update(dt,controls,elapsed);
            }else picker->update(dt);
            int width,height;SDL_GL_GetDrawableSize(window.handle,&width,&height);
            if(width>0 && height>0){
                if(game)game->draw(width,height);
                else picker->draw(width,height);
                if(!window.offscreen)SDL_GL_SwapWindow(window.handle);
            }
            if(renderer.graphics()!=appliedGraphics){
                if(renderer.graphics().vsync!=appliedGraphics.vsync)SDL_GL_SetSwapInterval(renderer.graphics().vsync);
                appliedGraphics=renderer.graphics();appliedGraphics.save(graphicsFile);
            }
            const int cap=std::array<int,5>{0,30,45,60,120}[appliedGraphics.fps];
            if(!focused)std::this_thread::sleep_for(std::chrono::milliseconds(30));
            else if(cap>0)std::this_thread::sleep_until(now+std::chrono::microseconds(1000000/cap));
            else if(dt<0.008)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return 0;
    }catch(const std::exception& error){reportLaunchError(error.what(),interactive);return 1;}
}
