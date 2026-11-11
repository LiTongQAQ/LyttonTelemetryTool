// Wheel buttons: the program lists the game controllers (DirectInput), lets the user pick a controller for each plugin
// (the Lytton Dash mod, later the program itself) and bind a button to each of its actions by pressing it, and saves
// the bindings to a small file the plugins read (%LOCALAPPDATA%\LyttonTelemetryTool\wheel_buttons.ini). Only reads
// the controllers, never the game; the mod that acts on its bindings runs only in offline sessions.
#pragma once

#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace wheel {

// Actions, as SimHub keeps them apart from the buttons: a plugin registers its actions here and the Game page lists
// each plugin with its own controller and a Bind button per action. Plugin id = ini section, action id = key in it;
// neither is ever renamed (saved bindings use them). A rotary knob turns into button presses, so its two directions
// can go to two actions.
struct PluginDef {
    const char *id;
    const char *title;
};
struct ActionDef {
    const char *plugin; // PluginDef::id
    const char *id;
    const char *label;
};
const std::vector<PluginDef> &plugins();
const std::vector<ActionDef> &actions();

struct Device {
    std::string guid, product, name; // instance and product GUID as text, product name
    int buttons = 0;
};

// one plugin's controller and buttons
struct Binding {
    std::string device, product, deviceName; // instance GUID (product GUID as fallback when the instance changes)
    std::map<std::string, int> button;       // action id -> button (1-based); missing = none
    int get(const std::string &id) const;
};
using Bindings = std::map<std::string, Binding>; // plugin id -> binding

// %LOCALAPPDATA%\LyttonTelemetryTool\wheel_buttons.ini
std::string bindingsPath();
Bindings load();
bool save(const Bindings &b);

class Reader {
public:
    ~Reader();
    void start(); // enumerates the controllers and reads the watched ones on a worker thread
    void stop();
    void rescan();
    std::vector<Device> devices() const;
    void watch(const std::set<std::string> &guids);
    std::vector<int> pressed(const std::string &guid) const; // buttons held now, 1-based
    // waiting for a press on one controller: the next newly pressed button, -1 while none
    void capture(const std::string &guid);
    void cancelCapture() { capturing_ = false; }
    bool capturing() const { return capturing_; }
    int captured() { return captured_.exchange(-1); }

private:
    void run();
    mutable std::mutex m_;
    std::thread t_;
    std::atomic<bool> stop_{false}, rescan_{true}, capturing_{false};
    std::atomic<int> captured_{-1};
    std::vector<Device> devices_;
    std::set<std::string> watched_;
    std::string captureDev_;
    std::map<std::string, std::vector<int>> pressed_;
};

} // namespace wheel
