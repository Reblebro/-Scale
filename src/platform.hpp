#pragma once
#include <cstdint>

struct SDL_Window;
struct SDL_GameController;
using SDL_GLContext = void*;
using Uint32 = std::uint32_t;
using Uint16 = std::uint16_t;
using Uint8 = std::uint8_t;
struct SDL_Keysym { int scancode; int sym; Uint16 mod; Uint32 unused; };
struct SDL_KeyboardEvent { Uint32 type,timestamp,windowID; Uint8 state,repeat,padding2,padding3; SDL_Keysym keysym; };
struct SDL_TextInputEvent { Uint32 type,timestamp,windowID; char text[32]; };
struct SDL_MouseMotionEvent { Uint32 type,timestamp,windowID,which,state; int x,y,xrel,yrel; };
struct SDL_MouseButtonEvent { Uint32 type,timestamp,windowID,which; Uint8 button,state,clicks,padding1; int x,y; };
struct SDL_MouseWheelEvent { Uint32 type,timestamp,windowID,which; int x,y; Uint32 direction; float preciseX,preciseY; int mouseX,mouseY; };
struct SDL_WindowEvent { Uint32 type,timestamp,windowID; Uint8 event,pad1,pad2,pad3; int data1,data2; };
struct SDL_AudioSpec {
    int freq;
    Uint16 format;
    Uint8 channels,silence;
    Uint16 samples,padding;
    Uint32 size;
    void (*callback)(void*,Uint8*,int);
    void* userdata;
};
union SDL_Event {
    Uint32 type;
    SDL_KeyboardEvent key;
    SDL_TextInputEvent text;
    SDL_MouseMotionEvent motion;
    SDL_MouseButtonEvent button;
    SDL_MouseWheelEvent wheel;
    SDL_WindowEvent window;
    std::uint64_t alignment;
    Uint8 padding[56];
};
static_assert(sizeof(SDL_Event)==56);
extern "C" {
void SDL_SetMainReady();
int SDL_Init(Uint32);
int SDL_NumJoysticks();
int SDL_IsGameController(int);
SDL_GameController* SDL_GameControllerOpen(int);
void SDL_GameControllerClose(SDL_GameController*);
int SDL_GameControllerGetAttached(SDL_GameController*);
std::int16_t SDL_GameControllerGetAxis(SDL_GameController*,int);
Uint8 SDL_GameControllerGetButton(SDL_GameController*,int);
int SDL_InitSubSystem(Uint32);
Uint32 SDL_OpenAudioDevice(const char*,int,const SDL_AudioSpec*,SDL_AudioSpec*,int);
void SDL_PauseAudioDevice(Uint32,int);
int SDL_QueueAudio(Uint32,const void*,Uint32);
Uint32 SDL_GetQueuedAudioSize(Uint32);
void SDL_Quit();
const char* SDL_GetCurrentVideoDriver();
int SDL_ShowSimpleMessageBox(Uint32,const char*,const char*,SDL_Window*);
void SDL_GL_ResetAttributes();
const char* SDL_GetError();
SDL_Window* SDL_CreateWindow(const char*,int,int,int,int,Uint32);
void SDL_DestroyWindow(SDL_Window*);
int SDL_SetWindowFullscreen(SDL_Window*,Uint32);
int SDL_SetRelativeMouseMode(int);
int SDL_GetRelativeMouseMode();
Uint32 SDL_GetRelativeMouseState(int*,int*);
Uint32 SDL_GetMouseState(int*,int*);
Uint32 SDL_GetWindowFlags(SDL_Window*);
void SDL_SetWindowGrab(SDL_Window*,int);
void SDL_WarpMouseInWindow(SDL_Window*,int,int);
int SDL_ShowCursor(int);
void SDL_StartTextInput();
void SDL_StopTextInput();
int SDL_PollEvent(SDL_Event*);
const Uint8* SDL_GetKeyboardState(int*);
const char* SDL_GetScancodeName(int);
void SDL_GetWindowSize(SDL_Window*,int*,int*);
int SDL_GL_SetAttribute(int,int);
SDL_GLContext SDL_GL_CreateContext(SDL_Window*);
void SDL_GL_DeleteContext(SDL_GLContext);
int SDL_GL_SetSwapInterval(int);
void SDL_GL_SwapWindow(SDL_Window*);
void SDL_GL_GetDrawableSize(SDL_Window*,int*,int*);
void* SDL_GL_GetProcAddress(const char*);
int SDL_SetHint(const char*,const char*);
}
namespace platform {
constexpr Uint32 InitVideo=0x20, OpenGL=0x2, Hidden=0x8, Resizable=0x20, Fullscreen=0x1001, HighDpi=0x2000;
constexpr Uint32 Quit=0x100, Window=0x200, KeyDown=0x300, TextInput=0x303, Motion=0x400, ButtonDown=0x401, Wheel=0x403;
constexpr int Center=0x2fff0000;
enum Key { A=4,B=5,C=6,D=7,E=8,F=9,G=10,H=11,I=12,M=16,N=17,P=19,Q=20,R=21,S=22,T=23,V=25,W=26,X=27,Z=29,
    One=30,Two=31,Three=32,Four=33,Five=34,Six=35,Seven=36, Enter=40,Escape=41,Backspace=42, Tab=43,
    Space=44, Minus=45, Plus=46,LeftBracket=47,RightBracket=48, F3=60,F7=64,F8=65,F11=68,Right=79,Left=80,Down=81,Up=82, Control=224, Shift=225, RControl=228, RAlt=230 };
}
