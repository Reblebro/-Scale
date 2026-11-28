#pragma once

#include "platform.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace input {
using namespace platform;
constexpr int FirstMouse=512;
constexpr int Primary=513,Sample=514,Secondary=515,WheelUp=520,WheelDown=521;
inline constexpr auto actions=std::to_array<std::pair<int,std::string_view>>({
    {W,"Move forward"}, {S,"Move back"}, {A,"Strafe left"}, {D,"Strafe right"},
    {Control,"Brisk movement"}, {Shift,"Crouch"}, {Space,"Jump / fly up"}, {Q,"Fly down"},
    {V,"Toggle flight"}, {Plus,"Shrink"}, {Minus,"Grow"}, {B,"Ascend a realm"},
    {Primary,"Mine / fire"}, {Secondary,"Interact / place / aim"}, {Sample,"Sample material"},
    {E,"Interact"}, {F,"Flashlight"}, {G,"Roll loot (fixed)"}, {C,"Camp workbench"}, {H,"Build materials"},
    {X,"Equip / cycle weapon"}, {Z,"Inspect integrated rifle sight"}, {I,"Mine shortcut"}, {Tab,"Inventory"},
    {R,"Generator / machine controls"}, {M,"Survey map"}, {P,"Skip day / night"},
    {LeftBracket,"Previous material"}, {RightBracket,"Next material"},
    {One,"Select dirt"}, {Two,"Select stone"}, {Three,"Select timber"}, {Four,"Select clay"},
    {Five,"Select cable"}, {Six,"Select lamp"}, {Seven,"Select switch"},
    {F3,"Performance display"}, {F7,"Render resolution"}, {F8,"Mouse capture mode"}, {F11,"Fullscreen"},
    {WheelUp,"Shrink with wheel"}, {WheelDown,"Grow with wheel"}
});
struct Bindings {
    std::array<int,actions.size()> keys{};
    std::filesystem::path file;
    explicit Bindings(std::filesystem::path file={}):file(std::move(file)){reset();load();}
    void reset(){
        for(std::size_t i=0;i<keys.size();i++)keys[i]=actions[i].first;
    }
    int physical(int logical) const{
        for(std::size_t i=0;i<keys.size();i++)if(actions[i].first==logical)return keys[i];
        return logical;
    }
    int logical(int physical) const{
        for(std::size_t i=0;i<keys.size();i++)if(keys[i]==physical)return actions[i].first;
        const int alias=physical==RControl?Control:physical==229?Shift:0;
        if(alias && this->physical(alias)==alias)return alias;
        return 0;
    }
    static bool assignable(int code){
        return (code>=4 && code<512 && code!=Escape && code!=Enter && code!=Up && code!=Down && code!=Left && code!=Right && code!=Backspace) ||
            (code>=Primary && code<=517) || code==WheelUp || code==WheelDown;
    }
    static bool heldAction(int action){
        return action==W || action==S || action==A || action==D || action==Control ||
            action==Shift || action==Space || action==Q || action==Secondary;
    }
    static bool validFor(int action,int physical){
        if(action==G || physical==G)return action==G && physical==G;
        return !heldAction(action) || (physical!=WheelUp && physical!=WheelDown);
    }
    bool assign(std::size_t index,int code){
        if(!validFor(actions[index].first,code))return false;
        const auto previous=std::find(keys.begin(),keys.end(),code);
        if(previous!=keys.end()){
            const auto other=std::size_t(previous-keys.begin());
            if(!validFor(actions[other].first,keys[index]))return false;
            *previous=keys[index];
        }
        keys[index]=code;
        return true;
    }
    bool down(int logical,const Uint8* keyboard,Uint32 buttons) const{
        const int code=physical(logical);
        if(code>=Primary && code<=517)return (buttons&(1u<<(code-Primary)))!=0;
        if(code<=0 || code>=FirstMouse)return false;
        return keyboard[code] || (logical==Control && code==Control && keyboard[RControl]) ||
            (logical==Shift && code==Shift && keyboard[229]);
    }
    std::string name(int action) const{
        const int code=physical(action);
        if(code==Primary)return "LMB";
        if(code==Secondary)return "RMB";
        if(code==Sample)return "MMB";
        if(code==WheelUp)return "Wheel up";
        if(code==WheelDown)return "Wheel down";
        if(code>=516 && code<=517)return "Mouse "+std::to_string(code-FirstMouse);
        const char* label=SDL_GetScancodeName(code);
        return label && *label?std::string(label):"Key "+std::to_string(code);
    }
    void load(){
        if(file.empty())return;
        std::ifstream config(file);
        std::string tag;
        if(!(config>>tag))return;
        if(tag=="SCALE-CONTROLS-1"){
            auto previous=keys;
            for(std::size_t i=0;i<actions.size();i++){
                if(actions[i].first==Tab)continue;
                if(!(config>>previous[i]) || !assignable(previous[i]))return;
            }
            const auto oldMine=std::find_if(actions.begin(),actions.end(),[](const auto& row){return row.first==F;});
            const auto miningIndex=std::size_t(oldMine-actions.begin());
            for(std::size_t i=0;i<actions.size();i++){
                const int action=actions[i].first;
                if(action==F || action==Tab)continue;
                const int code=action==I?(previous[miningIndex]==F?I:previous[miningIndex]):previous[i];
                if(code!=F && code!=Tab && code!=G)assign(i,code);
            }
            return;
        }
        if(tag!="SCALE-CONTROLS-2" && tag!="SCALE-CONTROLS-3")return;
        auto parsed=keys;
        for(int& code:parsed)if(!(config>>code) || !assignable(code))return;
        for(auto at=parsed.begin();at!=parsed.end();++at)if(std::find(parsed.begin(),at,*at)!=at)return;
        if(tag=="SCALE-CONTROLS-2"){
            const auto g=std::find_if(actions.begin(),actions.end(),[](const auto& row){return row.first==G;});
            const auto old=std::find(parsed.begin(),parsed.end(),G);
            const auto slot=std::size_t(g-actions.begin());
            if(old!=parsed.end() && std::size_t(old-parsed.begin())!=slot){
                const auto other=std::size_t(old-parsed.begin());
                const int action=actions[other].first;
                int replacement=parsed[slot];
                if(!validFor(action,replacement)){
                    replacement=action;
                    if(std::find(parsed.begin(),parsed.end(),replacement)!=parsed.end()){
                        for(int code=4;code<512;code++)if(assignable(code) && validFor(action,code) &&
                            std::find(parsed.begin(),parsed.end(),code)==parsed.end()){
                            replacement=code;
                            break;
                        }
                    }
                }
                parsed[other]=replacement;
            }
            parsed[slot]=G;
        }
        for(std::size_t i=0;i<parsed.size();i++)if(!validFor(actions[i].first,parsed[i]))return;
        keys=parsed;
    }
    bool save() const{
        if(file.empty())return true;
        std::error_code error;
        if(!file.parent_path().empty())std::filesystem::create_directories(file.parent_path(),error);
        if(error)return false;
        auto pending=file;
        pending+=".tmp";
        std::ofstream config(pending);
        config<<"SCALE-CONTROLS-3\n";
        for(int key:keys)config<<key<<' ';
        config<<'\n';
        config.close();
        if(!config)return false;
        std::filesystem::rename(pending,file,error);
        return !error;
    }
};
}
