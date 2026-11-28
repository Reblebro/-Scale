#include <random>
#include <charconv>
#include "world-picker.hpp"
#include "platform.hpp"
#include "world.hpp"
#include <charconv>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <cstring>

#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef near
#undef far
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
#include <cerrno>
#include <utility>
#include <memory>

namespace fs=std::filesystem;
namespace {
constexpr int MaxSlots=999;
constexpr std::size_t MaxName=24,VisibleRows=5;
enum PickerKey { Home=74,End=77 };

std::string cleanName(std::string_view input){
    std::string result;
    for(char c:input){
        if(result.size()>=MaxName)break;
        if(c>=32 && c<=126)result+=c;
    }
    return result;
}
std::string trimName(std::string name){
    const auto first=name.find_first_not_of(' ');
    if(first==std::string::npos)return {};
    return name.substr(first,name.find_last_not_of(' ')-first+1);
}
int indexOf(std::string_view name){
    int index=0;
    const auto result=std::from_chars(name.data(),name.data()+name.size(),index);
    if(result.ec!=std::errc{} || result.ptr!=name.data()+name.size() ||
        index<=0 || index>MaxSlots || std::to_string(index)!=name)return 0;
    return index;
}
std::string readName(const fs::path& path,int index){
    std::ifstream input(path);
    std::array<char,MaxName+1> line{};
    input.getline(line.data(),line.size());
    auto name=trimName(cleanName(line.data()));
    return name.empty()?"World "+std::to_string(index):name;
}
std::string playedDate(const fs::path& save){
    std::error_code problem;
    const auto when=fs::last_write_time(save,problem);
    if(problem)return {};
#ifdef _MSC_VER
    const auto timestamp=std::chrono::system_clock::to_time_t(std::chrono::clock_cast<std::chrono::system_clock>(when));
#else
    const auto timestamp=std::chrono::system_clock::to_time_t(std::chrono::file_clock::to_sys(when));
#endif
    std::tm local{};
#ifdef _WIN32
    if(::localtime_s(&local,&timestamp)!=0)return {};
#else
    if(!::localtime_r(&timestamp,&local))return {};
#endif
    std::array<char,32> text{};
    if(!std::strftime(text.data(),text.size(),"%b %d, %Y",&local))return {};
    return text.data();
}
struct Layout {
    float s,x,y,w,h,buttonY,rowY,rowH;
    explicit Layout(int width,int height){
        s=std::min(width/1080.f,height/660.f);
        x=(width-892*s)*0.5f;
        y=(height-540*s)*0.5f;
        w=439*s;h=524*s;
        rowY=y+106*s;rowH=61*s;
        buttonY=y+432*s;
    }
};
struct Area {
    float x,y,w,h;
    bool contains(float px,float py) const {return px>=x && py>=y && px<x+w && py<y+h;}
};

std::size_t firstRow(std::size_t cursor,std::size_t total){
    return cursor<VisibleRows?0:std::min(cursor-VisibleRows+1,total-VisibleRows);
}
constexpr Color gold{0.96f,0.77f,0.47f},pale{0.99f,0.89f,0.74f},
    matte{0.19f,0.115f,0.073f},subtle{0.34f,0.245f,0.17f},rule{0.58f,0.36f,0.19f},
    lightRule{0.78f,0.54f,0.29f},rust{0.48f,0.18f,0.10f};
void frame(Renderer& r,const Area& box,float s){
    const auto [x,y,w,h]=box;
    r.vellum(x,y,w,h,{0.87f,0.71f,0.52f,0.94f});
    for(auto inset:{0.f,3.f,8.f}){
        const float d=inset*s,stroke=std::max(0.8f,s);
        const Color color=inset==3?lightRule:rule;
        r.line(x+d,y+d,w-2*d,stroke,color);
        r.line(x+d,y+h-d,w-2*d,stroke,color);
        r.line(x+d,y+d,stroke,h-2*d,color);
        r.line(x+w-d,y+d,stroke,h-2*d,color);
    }
    for(float px:{x+8*s,x+w-8*s})for(float py:{y+8*s,y+h-8*s}){
        r.diamond(px,py,5.5f*s,5.5f*s,rule);
        r.diamond(px,py,2.5f*s,2.5f*s,gold);
    }
}
void button(Renderer& r,Area a,float s,std::string_view text,bool primary){
    r.rect(a.x,a.y,a.w,a.h,primary?matte:Color{0.73f,0.54f,0.34f});
    r.line(a.x,a.y,a.w,1.3f*s,lightRule);
    r.line(a.x,a.y+a.h-1.3f*s,a.w,1.3f*s,rule);
    const float font=0.88f*s;
    r.text(a.x+(a.w-r.textWidth(text,font))*0.5f,a.y+5*s,text,primary?pale:matte,font);
}
}

std::vector<WorldEntry> WorldLibrary::list() const {
    std::vector<WorldEntry> result;
    if(original.empty())return result;
    std::error_code problem;
    if(fs::is_regular_file(fs::symlink_status(original,problem)))result.push_back({original,"Original World",true,playedDate(original)});
    const auto folder=original.parent_path()/"worlds";
    std::vector<std::pair<int,WorldEntry>> found;
    problem.clear();
    if(!fs::is_directory(fs::symlink_status(folder,problem)))return result;
    for(fs::directory_iterator it(folder,fs::directory_options::skip_permission_denied,problem),end;it!=end;it.increment(problem)){
        if(problem)break;
        const auto& item=*it;
        const int index=indexOf(item.path().filename().string());
        std::error_code access;
        if(!index || !fs::is_directory(item.symlink_status(access)))continue;
        const auto save=item.path()/"world.save";
        const auto meta=item.path()/"name.txt";
        const bool saved=fs::is_regular_file(fs::symlink_status(save,access));
        if(!saved && !fs::is_regular_file(fs::symlink_status(meta,access)))continue;
        found.push_back({index,{save,readName(meta,index),saved,saved?playedDate(save):std::string{}}});
    }
    std::sort(found.begin(),found.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    for(auto& row:found)result.push_back(std::move(row.second));
    return result;
}

WorldEntry WorldLibrary::create(std::string_view proposed,std::string_view seedText) const{
    if(original.empty())throw std::runtime_error("No writable save directory is configured.");
    std::uint64_t seedValue;
    if(seedText.empty()){
        std::random_device random;
        seedValue=(std::uint64_t(random())<<32)^random();
    }else{
        const auto parsed=std::from_chars(seedText.data(),seedText.data()+seedText.size(),seedValue);
        if(parsed.ec!=std::errc{} || parsed.ptr!=seedText.data()+seedText.size())
            throw std::runtime_error("Enter an unsigned 64-bit seed.");
    }
    const auto entries=list();
    auto name=trimName(cleanName(proposed));
    if(name.empty()){
        int n=int(entries.size())+1;
        do{name="World "+std::to_string(n++);}
        while(std::any_of(entries.begin(),entries.end(),[&](const auto& e){return e.name==name;}));
    }
    if(std::any_of(entries.begin(),entries.end(),[&](const auto& row){return row.name==name;}))
        throw std::runtime_error("A world already uses that name.");
    const auto folder=original.parent_path()/"worlds";
    fs::create_directories(folder);
    if(!fs::is_directory(fs::symlink_status(folder)))throw std::runtime_error("Invalid worlds directory.");
    for(int index=1;index<=MaxSlots;index++){
        const auto destination=folder/std::to_string(index);
        std::error_code error;
        if(!fs::create_directory(destination,error)){
            if(error && error!=std::errc::file_exists)throw fs::filesystem_error("Create world",destination,error);
            if(!fs::is_directory(fs::symlink_status(destination)))continue;
            // Cleared slots retain their lock file so they can be reused safely.
            bool vacant=true;
            for(const auto& entry:fs::directory_iterator(destination))
                if(entry.path().filename()!="world.save.lock"){vacant=false;break;}
            if(!vacant)continue;
        }
        std::unique_ptr<SaveLock> guard;
        try{guard=std::make_unique<SaveLock>(destination/"world.save");}
        catch(const std::runtime_error&){continue;}
        // Recheck under the lock in case another picker created a world.
        bool vacant=true;
        for(const auto& entry:fs::directory_iterator(destination))
            if(entry.path().filename()!="world.save.lock"){vacant=false;break;}
        if(!vacant)continue;
        const auto pending=destination/"name.part";
        const auto meta=destination/"name.txt";
        try{
            std::ofstream out(pending);
            out.exceptions(std::ios::badbit|std::ios::failbit);
            out<<name<<'\n';
            out.close();
            std::ofstream seedFile(destination/"seed.txt");
            seedFile.exceptions(std::ios::badbit|std::ios::failbit);
            seedFile<<seedValue<<'\n';
            seedFile.close();
            fs::rename(pending,meta);
        }catch(...){
            fs::remove(destination/"seed.txt",error);
            fs::remove(pending,error);
            // Keep the lock inode for future slot reuse.
            throw;
        }
        return {destination/"world.save",name,false,{}};
    }
    throw std::runtime_error("The world library is full.");
}

void WorldLibrary::erase(const std::vector<fs::path>& saves) const{
    const auto entries=list();
    std::vector<fs::path> selected;
    for(const auto& save:saves){
        if(std::none_of(entries.begin(),entries.end(),[&](const auto& e){return e.save==save;}))
            throw std::runtime_error("World list changed; reopen the picker.");
        if(std::find(selected.begin(),selected.end(),save)==selected.end())selected.push_back(save);
    }
    // Acquire every selected lock before deleting anything. A world open in
    // another process aborts the operation before any files are removed.
    std::vector<std::unique_ptr<SaveLock>> locks;
    for(const auto& save:selected){
        if(!fs::is_directory(fs::symlink_status(save.parent_path())))
            throw std::runtime_error("Unsafe world directory.");
        locks.push_back(std::make_unique<SaveLock>(save));
    }
    for(const auto& save:selected){
        std::vector<fs::path> files{save,fs::path(save.string()+".tmp"),fs::path(save.string()+".pre-0.6.0")};
        // Original-world metadata may only be the seed. Never remove settings,
        // other campaigns, arbitrary files, or an entire directory recursively.
        files.push_back(save.parent_path()/"seed.txt");
        if(save!=original){files.push_back(save.parent_path()/"name.txt");files.push_back(save.parent_path()/"name.part");}
        for(const auto& path:files){
            std::error_code statusError;
            const auto status=fs::symlink_status(path,statusError);
            if(statusError==std::errc::no_such_file_or_directory)continue;
            if(statusError)throw fs::filesystem_error("Inspect world file",path,statusError);
            if(!fs::exists(status))continue;
            if(!fs::is_regular_file(status))throw std::runtime_error("Unsafe world file; nothing followed.");
        }
    }
    for(const auto& save:selected){
        std::vector<fs::path> files{save,fs::path(save.string()+".tmp"),fs::path(save.string()+".pre-0.6.0"),save.parent_path()/"seed.txt"};
        if(save!=original){files.push_back(save.parent_path()/"name.txt");files.push_back(save.parent_path()/"name.part");}
        for(const auto& path:files)fs::remove(path);
    }
}

SaveLock::SaveLock(const fs::path& save){
    if(save.empty())return;
    if(!save.parent_path().empty())fs::create_directories(save.parent_path());
    auto path=save;
    path+=".lock";
#ifdef _WIN32
    const auto handle=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(handle==INVALID_HANDLE_VALUE){
        if(GetLastError()==ERROR_SHARING_VIOLATION)
            throw std::runtime_error("This world is open in another !Scale window.");
        throw std::runtime_error("Could not open the world lock file.");
    }
    lockHandle=handle;
#else
    descriptor=::open(path.c_str(),O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
    if(descriptor<0)throw std::runtime_error(std::string("Could not lock the save: ")+std::strerror(errno));
    if(::flock(descriptor,LOCK_EX|LOCK_NB)<0){
        const int failure=errno;
        ::close(descriptor);
        descriptor=-1;
        if(failure==EWOULDBLOCK)throw std::runtime_error("This world is open in another !Scale window.");
        throw std::runtime_error(std::string("Could not lock the save: ")+std::strerror(failure));
    }
#endif
}
SaveLock::~SaveLock(){
#ifdef _WIN32
    if(lockHandle)CloseHandle(lockHandle);
#else
    if(descriptor>=0)::close(descriptor);
#endif
}

WorldPicker::WorldPicker(Renderer& draw,const fs::path& original,const fs::path& recent)
    :renderer(draw),library(original),entries(library.list()){
    for(std::size_t i=0;i<entries.size();i++)if(entries[i].save==recent)cursor=i;
    Geometry land,accent;
    const auto height=[](int x,int z){
        return 0.20+0.17*std::cos(x*0.94+std::sin(z*0.99))+0.13*std::sin(z*1.22-x*0.46);
    };
    for(int z=-5;z<=5;z++)for(int x=-5;x<=5;x++){
        const double radius=std::hypot(double(x),double(z));
        if(radius>5.2+0.3*std::sin(x*2.0+z))continue;
        const double y=height(x,z);
        const Vec center{x*0.49,y,z*0.49};
        const double thick=0.35+(1-radius/5.80)*0.94;
        land.box(center+Vec{-0.246,-thick,-0.246},center+Vec{0.246,-0.076,0.246},Stone,{0.66f,0.75f,0.79f});
        land.box(center+Vec{-0.246,-0.079,-0.246},center+Vec{0.246,0,0.246},Grass,{0.89f,1.04f,0.83f});
        if((x==-3 && z>-3 && z<4)||(z==2 && x>=-3 && x<=2))
            land.box(center+Vec{-0.246,0,-0.246},center+Vec{0.246,0.023,0.246},CampTile,{0.91f,0.96f,0.98f});
        if((x*x+z*z)>5 && (x*x+z*z)<22 && (x*7+z*11)%17==0){
            land.tube(center,center+Vec{0,0.65,0},0.058,Timber,{1,0.9f,0.76f},6);
            land.stone(center+Vec{0,0.73,0},0.37,Leaves,{0.82f,1.10f,0.86f},5,7);
        }
    }
    const Vec base{0.0,0.72,-0.42};
    for(int i=0;i<12;i++){
        const auto p=[&](int k){return base+Vec{0.63*std::cos(k*Pi/6),0.63*std::sin(k*Pi/6),0};};
        land.tube(p(i),p(i+1),0.055,BrassTexture,{0.94f,0.90f,0.74f},6);
        accent.tube(p(i),p(i+1),0.022,FluxTexture,{0.69f,0.99f,1.02f},5);
    }
    for(double x:{-1.3,0.6,1.45})
        land.stone({x,-0.77,std::sin(x)*0.54},0.19,Quartz,{0.85f,0.88f,1.06f},4,7);
    accent.stone({0.0,1.48,-0.42},0.10,FluxTexture,{0.74f,1.04f,1.14f},5,8);
    island.upload(land);beacon.upload(accent);
}
WorldPicker::~WorldPicker(){if(editing)SDL_StopTextInput();}
void WorldPicker::setEditing(bool value){
    editing=value;
    notice.clear();
    if(editing){name.clear();seed.clear();editingSeed=false;SDL_StartTextInput();}
    else SDL_StopTextInput();
}
void WorldPicker::requestDelete(bool all){
    if(editing || entries.empty() || (!all && cursor>=entries.size()))return;
    deleteMode=all?2:1;notice.clear();
}
void WorldPicker::confirmDelete(){
    if(!deleteMode)return;
    std::vector<fs::path> paths;
    if(deleteMode==2){for(const auto& entry:entries)paths.push_back(entry.save);}
    else if(cursor<entries.size())paths.push_back(entries[cursor].save);
    try{library.erase(paths);notice=deleteMode==2?"All worlds cleared":"World deleted";}
    catch(const std::exception& problem){notice=problem.what();}
    deleteMode=0;chosen.reset();entries=library.list();cursor=std::min(cursor,entries.size());
}
void WorldPicker::activate(){
    if(editing){
        try{
            const auto result=library.create(name,seed);
            chosen=result.save;
            setEditing(false);
            entries=library.list();
            cursor=entries.empty()?0:entries.size()-1;
        }catch(const std::exception& issue){notice=issue.what();}
    }else if(cursor<entries.size())chosen=entries[cursor].save;
    else setEditing(true);
}
void WorldPicker::move(int delta){
    if(editing || deleteMode)return;
    cursor=std::size_t(std::clamp(int(cursor)+delta,0,int(entries.size())));
    notice.clear();
}
void WorldPicker::key(int code){
    using namespace platform;
    if(deleteMode){
        if(code==platform::Escape){deleteMode=0;notice.clear();}
        else if(code==Enter)confirmDelete();
        return;
    }
    if(code==platform::Escape){
        if(editing)setEditing(false);
        else leave=true;
        return;
    }
    if(code==Enter){activate();return;}
    if(editing){
        if(code==Tab){editingSeed=!editingSeed;return;}
        auto& value=editingSeed?seed:name;
        if(code==Backspace && !value.empty())value.pop_back();
        return;
    }
    if(code==Up || code==W)move(-1);
    if(code==Down || code==S)move(1);
    if(code==Home)move(-MaxSlots-1);
    if(code==End)move(MaxSlots+1);
    if(code==N){cursor=entries.size();setEditing(true);}
}
void WorldPicker::text(std::string_view input){
    if(!editing)return;
    if(editingSeed){
        for(char ch:input)if(ch>='0' && ch<='9' && seed.size()<20)seed+=ch;
    }else name=cleanName(name+std::string(input));
    notice.clear();
}
void WorldPicker::wheel(double amount){if(amount!=0)move(amount>0?-1:1);}
void WorldPicker::click(float x,float y,int width,int height){
    const Layout l(width,height);
    const float s=l.s;
    const Area primary{l.x+26*s,l.buttonY,216*s,42*s};
    const Area secondary{l.x+253*s,l.buttonY,160*s,42*s};
    if(deleteMode){
        if(primary.contains(x,y))confirmDelete();
        else if(secondary.contains(x,y)){deleteMode=0;notice.clear();}
        return;
    }
    if(!editing && Area{l.x+515*s,l.y+334*s,284*s,38*s}.contains(x,y)){requestDelete(false);return;}
    if(!editing && Area{l.x+515*s,l.y+382*s,284*s,38*s}.contains(x,y)){requestDelete(true);return;}
    if(primary.contains(x,y)){activate();return;}
    if(secondary.contains(x,y) && (editing || cursor<entries.size())){
        if(editing)setEditing(false);
        else{cursor=entries.size();setEditing(true);}
        return;
    }
    if(editing){
        if(y>=l.y+142*s && y<l.y+190*s)editingSeed=false;
        if(y>=l.y+240*s && y<l.y+288*s)editingSeed=true;
        return;
    }
    const auto first=firstRow(cursor,entries.size()+1);
    for(std::size_t row=0;row<VisibleRows && first+row<=entries.size();row++){
        const Area area{l.x+25*s,l.rowY+row*l.rowH,l.w-50*s,l.rowH-7*s};
        if(!area.contains(x,y))continue;
        if(cursor==first+row)activate();
        else{cursor=first+row;notice.clear();}
        return;
    }
}
void WorldPicker::update(double dt){angle=std::fmod(angle+dt*0.20,2*Pi);}
std::optional<fs::path> WorldPicker::selection(){return std::exchange(chosen,std::nullopt);}

void WorldPicker::draw(int width,int height){
    const Layout l(width,height);
    const float s=l.s;
    const Vec eye{6.6,4.9,10.2},dir=unit(Vec{0,0,-0.05}-eye);
    const auto right=unit(cross(dir,{0,1,0}));
    View view{eye,dir,right,cross(right,dir)};
    view.sky={0.047f,0.075f,0.086f};view.ambient=0.94f;view.near=0.15;view.far=80;view.fog=65;view.fov=1.25;
    renderer.start(view,width,height);
    const auto vista=right*6.8+Vec{0,-0.43,0};
    renderer.draw(island,vista,1.87,angle);
    renderer.draw(beacon,vista,1.87,angle,nullptr,1.38f);
    frame(renderer,{l.x,l.y,l.w,l.h},s);
    renderer.rect(l.x+9*s,l.y+9*s,l.w-18*s,71*s,matte);
    renderer.title(l.x+26*s,l.y+15*s,deleteMode?"Delete worlds?":editing?"New World":"Choose a World",gold,1.31f*s);
    renderer.line(l.x+24*s,l.y+66*s,l.w-48*s,s,lightRule);

    if(deleteMode){
        renderer.text(l.x+26*s,l.y+115*s,deleteMode==2?"Clear ALL saved worlds?":"Delete selected world?",rust,1.03f*s);
        renderer.text(l.x+26*s,l.y+165*s,deleteMode==2?std::to_string(entries.size())+" worlds will be deleted":entries[cursor].name,matte,.94f*s);
        renderer.text(l.x+26*s,l.y+216*s,"This permanently removes progress.",matte,.80f*s);
        renderer.text(l.x+26*s,l.y+251*s,"Settings and game assets are kept.",matte,.80f*s);
        renderer.text(l.x+26*s,l.y+294*s,"Close any other open world first.",subtle,.78f*s);
        button(renderer,{l.x+26*s,l.buttonY,216*s,42*s},s,deleteMode==2?"Clear all worlds":"Delete world",true);
        button(renderer,{l.x+253*s,l.buttonY,160*s,42*s},s,"Cancel",false);
        renderer.text(l.x+26*s,l.y+485*s,"ENTER confirm   |   ESC cancel",subtle,.78f*s);
    }else if(editing){
        renderer.text(l.x+26*s,l.y+106*s,"World name",matte,0.9f*s);
        renderer.rect(l.x+26*s,l.y+142*s,l.w-52*s,48*s,matte);
        renderer.text(l.x+40*s,l.y+150*s,name.empty()?"Leave blank for a default name":name,name.empty()?gold:pale,0.94f*s);
        if(!editingSeed && !name.empty() && std::sin(angle*30)>0)
            renderer.rect(l.x+42*s+renderer.textWidth(name,0.94f*s),l.y+153*s,2*s,21*s,gold);
        renderer.text(l.x+26*s,l.y+205*s,"World seed",matte,0.90f*s);
        renderer.rect(l.x+26*s,l.y+240*s,l.w-52*s,48*s,matte);
        renderer.text(l.x+40*s,l.y+249*s,seed.empty()?"Random when blank":seed,editingSeed?gold:pale,0.94f*s);
        renderer.text(l.x+26*s,l.y+304*s,"TAB switches between name and seed",subtle,0.80f*s);
        button(renderer, {l.x+26*s,l.buttonY,216*s,42*s},s,"Create world",true);
        button(renderer, {l.x+253*s,l.buttonY,160*s,42*s},s,"Cancel",false);
        renderer.text(l.x+26*s,l.y+485*s,"ENTER create   |   ESC cancel",subtle,0.78f*s);
    }else{
        const auto first=firstRow(cursor,entries.size()+1);
        for(std::size_t row=0;row<VisibleRows && first+row<=entries.size();row++){
            const auto index=first+row;
            const bool active=index==cursor,isNew=index==entries.size();
            const float top=l.rowY+row*l.rowH;
            renderer.rect(l.x+25*s,top,l.w-50*s,l.rowH-7*s,active?Color{0.31f,0.18f,0.106f}:Color{0.74f,0.57f,0.39f,0.55f});
            if(active)renderer.rect(l.x+25*s,top,3*s,l.rowH-7*s,gold);
            renderer.text(l.x+39*s,top+3*s,isNew?"+ New world":entries[index].name,active?pale:matte,0.95f*s);
            const std::string detail=isNew?"Start a separate save":!entries[index].saved?"Ready to start":
                entries[index].played.empty()?"Saved progress":"Last saved "+entries[index].played;
            renderer.text(l.x+39*s,top+29*s,detail,active?gold:subtle,0.70f*s);
        }
        if(entries.size()+1>VisibleRows){
            const float span=VisibleRows*l.rowH-7*s;
            const float grip=span*VisibleRows/(entries.size()+1);
            const float top=l.rowY+(span-grip)*first/(entries.size()+1-VisibleRows);
            renderer.rect(l.x+l.w-18*s,l.rowY,2*s,span,rule);
            renderer.rect(l.x+l.w-19*s,top,4*s,grip,lightRule);
        }
        button(renderer,{l.x+26*s,l.buttonY,216*s,42*s},s,cursor<entries.size()?"Play world":"Create world",true);
        if(cursor<entries.size())button(renderer,{l.x+253*s,l.buttonY,160*s,42*s},s,"New world",false);
        renderer.text(l.x+26*s,l.y+485*s,"UP / DOWN select    ENTER open    ESC quit",subtle,0.69f*s);
    }
    if(!notice.empty()){
        const auto show=notice.substr(0,42);
        renderer.rect(l.x+25*s,l.buttonY-32*s,l.w-50*s,29*s,{0.88f,0.67f,0.46f});
        renderer.text(l.x+34*s,l.buttonY-30*s,show,rust,0.71f*s);
    }
    renderer.title(l.x+589*s,l.y+39*s,"!Scale",gold,1.62f*s);
    const std::string_view sub=(!editing && cursor<entries.size())
        ?std::string_view(entries[cursor].name):std::string_view("A fresh expedition");
    const float titleCenter=l.x+657*s;
    renderer.text(titleCenter-renderer.textWidth(sub,0.94f*s)*0.5f,l.y+100*s,sub,pale,0.94f*s);
    if(!editing && !deleteMode && !entries.empty()){
        if(cursor<entries.size())button(renderer,{l.x+515*s,l.y+334*s,284*s,38*s},s,"Delete selected world",false);
        button(renderer,{l.x+515*s,l.y+382*s,284*s,38*s},s,"Clear all worlds",false);
    }
    renderer.text(l.x+602*s,l.y+440*s,"v0.7.0",gold,0.84f*s);
    renderer.text(l.x+602*s,l.y+475*s,"Reblebro4200",pale,0.78f*s);
    renderer.finishHud();
}

void WorldPicker::verify(){
    using namespace platform;
    if(entries.size()!=1 || entries.front().name!="Original World")throw std::runtime_error("Picker legacy option missing");
    key(Down);key(Enter);
    text("   ");text("Second expedition");
    key(Backspace);text("n");
    key(Enter);
    const auto path=selection();
    if(!path || path==entries.front().save || entries.back().name!="Second expedition")
        throw std::runtime_error("Picker did not create the named independent world");
    key(Home);key(Enter);
    if(selection()!=std::optional(entries.front().save))throw std::runtime_error("Picker did not reopen the original world");
}
namespace {
struct TestFolder {
    fs::path folder;
    explicit TestFolder(const char* prefix){
        std::random_device random;
        for(int attempt=0;attempt<16;attempt++){
            folder=fs::temp_directory_path()/(std::string(prefix)+std::to_string(random()));
            if(fs::create_directory(folder))return;
        }
        throw std::runtime_error("Could not create a temporary test library.");
    }
    ~TestFolder(){std::error_code ignored;fs::remove_all(folder,ignored);}
};
}
void worldLibraryTests(){
    TestFolder cleanup("scale-worlds-");
    const auto original=cleanup.folder/"world.save";
    std::ofstream(original)<<"legacy data\n";
    WorldLibrary library(original);
    const auto secondary=library.create("  Delbert's Second  ","18446744073709551615");
    if(secondary.save==original || secondary.name!="Delbert's Second")
        throw std::runtime_error("World creation did not isolate save paths");
    std::ofstream(secondary.save)<<"second data\n";
    const auto entries=library.list();
    if(entries.size()!=2 || entries[0].save!=original || entries[1].save!=secondary.save ||
        !entries[0].saved || !entries[1].saved)throw std::runtime_error("World library failed to discover existing saves");
    std::string before;
    std::ifstream legacy(original);
    std::getline(legacy,before);
    legacy.close();
    if(before!="legacy data")throw std::runtime_error("World library altered the legacy save");
    {
    SaveLock first(original),second(secondary.save);
    bool held=false;
    try{SaveLock duplicate(original);}catch(const std::runtime_error&){held=true;}
    if(!held)throw std::runtime_error("World library allowed concurrent writes to one save");
    bool refused=false;
    try{library.erase({original,secondary.save});}catch(const std::runtime_error&){refused=true;}
    if(!refused || !fs::exists(original) || !fs::exists(secondary.save))throw std::runtime_error("Locked world was deleted");
    }
    std::ifstream seed(secondary.save.parent_path()/"seed.txt");
    std::string recorded;
    if(!(seed>>recorded) || recorded!="18446744073709551615")throw std::runtime_error("World seed was not persisted");
    const auto unnamed=library.create("");
    if(unnamed.name!="World 3")throw std::runtime_error("Unnamed world was not given a unique default");
    seed.close();
    const auto settings=cleanup.folder/"controls.txt";
    std::ofstream(settings)<<"keep me";
    library.erase({secondary.save});
    if(library.list().size()!=2 || fs::exists(secondary.save) || !fs::exists(original))
        throw std::runtime_error("Single deletion did not isolate worlds");
    library.erase({original,unnamed.save});
    if(!library.list().empty() || !fs::exists(settings))throw std::runtime_error("Clear worlds damaged library settings");
    const auto reused=library.create("Reused");
    if(reused.save!=secondary.save)throw std::runtime_error("Cleared slots cannot be reused");
}

void worldPickerPlayTests(Renderer& renderer,const fs::path& preview){
    TestFolder cleanup("scale-picker-");
    const auto save=cleanup.folder/"world.save";
    std::ofstream(save)<<"FRONTIER 3\nend\n";
    WorldPicker picker(renderer,save);
    if(!preview.empty()){
        picker.draw(1280,720);
        renderer.screenshot(preview.string()+"-world-picker.ppm");
        picker.key(platform::N);
        picker.text("Delbert's Second");
        picker.draw(1280,720);
        renderer.screenshot(preview.string()+"-new-world.ppm");
        picker.key(platform::Escape);picker.key(Home);
    }
    picker.verify();
    // Exercise the actual confirmation buttons, not only the storage API.
    const Layout l(1280,720);
    picker.click(l.x+650*l.s,l.y+400*l.s,1280,720);
    picker.key(platform::Escape);
    if(WorldLibrary(save).list().size()!=2)throw std::runtime_error("Cancel deleted worlds");
    picker.click(l.x+650*l.s,l.y+400*l.s,1280,720);
    if(!preview.empty()){
        picker.draw(1280,720);
        renderer.screenshot(preview.string()+"-clear-worlds.ppm");
    }
    picker.click(l.x+100*l.s,l.buttonY+10*l.s,1280,720);
    if(!WorldLibrary(save).list().empty())throw std::runtime_error("Clear worlds button did not clear library");
    std::cout<<"Verified world picker selection, separate named saves and confirmed world deletion.\n";
}
