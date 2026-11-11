// Starting F1 25 from the program: online through Steam (the stock game, EA AntiCheat on), or offline with mods
// through the Swofty Launcher's command line (`swofty run --apply`). The program never touches the game itself:
// Swofty applies the profile for the session and puts the game folder back when it ends; Swofty (AGPL-3.0) is
// installed by the user and only called, never shipped. All Swofty calls run on a worker thread.
#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace launcher {

constexpr const char *STEAM_APP_ID = "3059520"; // F1 25

struct Addon {
    std::string id, version, state, name; // state: enabled / disabled / built in
    bool enabled() const { return state == "enabled" || state == "built in"; }
    bool builtIn() const { return state == "built in"; }
    bool shader() const { return state == "shaders"; } // a shader config: chosen with setShader(), not switched on
};

struct Shader {
    std::string id, name; // id as `swofty shaders use` takes it
};

struct Status {
    bool checked = false;     // a refresh has finished
    bool found = false;       // swofty.exe found
    std::string exe, version; // its path and `swofty --version`
    std::vector<std::string> profiles;
    std::string activeProfile; // Swofty's own active profile
    std::vector<Addon> addons; // of the profile asked for in refresh()
    std::string addonsProfile;
    std::vector<Shader> shaders; // configs available to that profile
    std::string activeShader;    // its chosen config, "off" = none
    bool vanilla = false;      // doctor: the game folder matches the baseline
    std::string gameDir, gameBuild;
    bool consent = false, extension = false; // offline play: terms accepted, extension installed
    bool gameRunning = false;
};

class Launcher {
public:
    ~Launcher();
    // Re-reads everything from Swofty in the background; `profile` = whose addons to list (empty: Swofty's active)
    void refresh(const std::string &profile);
    void setAddon(const std::string &profile, const std::string &id, bool on);
    void playOnline(bool vr);
    void playOffline(const std::string &profile, bool vr);
    void setShader(const std::string &profile, const std::string &id); // id or "off"
    void restore();          // swofty restore: game folder back to stock
    void openSwofty() const; // the Swofty Launcher window (its one-time offline setup lives there)

    Status status() const;
    bool busy() const;
    std::string message() const; // the last result or error, for the page

private:
    void work(std::function<void()> fn);
    void say(const std::string &m);
    void doRefresh(const std::string &profile);

    mutable std::mutex m_;
    Status st_;
    std::string msg_;
    bool busy_ = false;
    std::thread th_;
};

// The exe found by find(): PATH first, then the installer's default folder; empty when missing.
std::string findSwofty();

} // namespace launcher
