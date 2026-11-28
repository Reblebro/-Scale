#pragma once
#include "render.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

struct WorldEntry {
    std::filesystem::path save;
    std::string name;
    bool saved{};
    std::string played;
};

class WorldLibrary {
    std::filesystem::path original;
public:
    explicit WorldLibrary(std::filesystem::path save):original(std::move(save)) {}
    std::vector<WorldEntry> list() const;
    WorldEntry create(std::string_view name,std::string_view seed={}) const;
    // Only registered worlds in this library, never the settings or assets.
    void erase(const std::vector<std::filesystem::path>& saves) const;
};

class SaveLock {
#ifdef _WIN32
    void* lockHandle=nullptr;
#else
    int descriptor=-1;
#endif
public:
    explicit SaveLock(const std::filesystem::path& save);
    ~SaveLock();
    SaveLock(const SaveLock&)=delete;
    SaveLock& operator=(const SaveLock&)=delete;
};

class WorldPicker {
    Renderer& renderer;
    WorldLibrary library;
    std::vector<WorldEntry> entries;
    std::string seed;
    bool editingSeed{};
    std::string name,notice;
    std::size_t cursor{};
    bool editing{},leave{};
    int deleteMode{}; // 0 normal, 1 selected, 2 all
    void requestDelete(bool all);
    void confirmDelete();
    double angle{};
    Mesh island,beacon;
    std::optional<std::filesystem::path> chosen;
    void activate();
    void setEditing(bool value);
    void move(int delta);
public:
    WorldPicker(Renderer& draw,const std::filesystem::path& original,const std::filesystem::path& recent={});
    ~WorldPicker();
    void key(int key);
    void text(std::string_view input);
    void wheel(double amount);
    void click(float x,float y,int width,int height);
    void update(double dt);
    void draw(int width,int height);
    void error(std::string message){notice=std::move(message);}
    std::optional<std::filesystem::path> selection();
    bool finished() const {return leave;}
    void verify();
};
void worldLibraryTests();
void worldPickerPlayTests(Renderer& renderer,const std::filesystem::path& preview={});
