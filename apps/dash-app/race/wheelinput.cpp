#include "wheelinput.hpp"

#include <windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace wheel {

const std::vector<PluginDef> &plugins()
{
    static const std::vector<PluginDef> p = {
        {"lytton_dash", "Lytton Dash (wheel screen mod, offline)"},
    };
    return p;
}

const std::vector<ActionDef> &actions()
{
    static const std::vector<ActionDef> a = {
        {"lytton_dash", "page1", "Page 1: driving"},
        {"lytton_dash", "page2", "Page 2: car settings, race"},
    };
    return a;
}

int Binding::get(const std::string &id) const
{
    const auto it = button.find(id);
    return it == button.end() ? 0 : it->second;
}

namespace {
std::string guidText(const GUID &g)
{
    char b[64];
    std::snprintf(b, sizeof b, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", g.Data1, g.Data2, g.Data3,
                  g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return b;
}
std::string utf8(const wchar_t *w)
{
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}
} // namespace

std::string bindingsPath()
{
    const char *la = std::getenv("LOCALAPPDATA");
    std::string dir = std::string(la ? la : ".") + "\\LyttonTelemetryTool";
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir + "\\wheel_buttons.ini";
}

// [plugin] sections of device=, product=, device_name= and action=button lines. Keys before any section (the first
// format, Lytton Dash only) count as [lytton_dash].
Bindings load()
{
    Bindings all;
    std::ifstream f(bindingsPath());
    std::string line, sec = "lytton_dash";
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') {
            const auto e = line.find(']');
            if (e != std::string::npos) sec = line.substr(1, e - 1);
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        Binding &b = all[sec];
        if (k == "device") b.device = v;
        else if (k == "product") b.product = v;
        else if (k == "device_name") b.deviceName = v;
        else if (std::atoi(v.c_str()) > 0) b.button[k] = std::atoi(v.c_str());
    }
    return all;
}

bool save(const Bindings &all)
{
    std::ostringstream o;
    o << "# Lytton Telemetry Tool: wheel buttons, one [section] per plugin (buttons 1-based)\n";
    for (const auto &[sec, b] : all) {
        if (b.device.empty()) continue;
        o << "\n[" << sec << "]\ndevice=" << b.device << "\nproduct=" << b.product << "\ndevice_name=" << b.deviceName << "\n";
        for (const auto &[id, n] : b.button)
            if (n > 0) o << id << "=" << n << "\n";
    }
    // write a temporary file and swap it in: a plugin may read it at any moment
    const std::string p = bindingsPath(), tmp = p + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << o.str();
    }
    return MoveFileExA(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

Reader::~Reader() { stop(); }

void Reader::start()
{
    if (t_.joinable()) return;
    stop_ = false;
    rescan_ = true;
    t_ = std::thread([this] { run(); });
}

void Reader::stop()
{
    stop_ = true;
    if (t_.joinable()) t_.join();
}

void Reader::rescan() { rescan_ = true; }

std::vector<Device> Reader::devices() const
{
    std::lock_guard<std::mutex> l(m_);
    return devices_;
}

void Reader::watch(const std::set<std::string> &guids)
{
    std::lock_guard<std::mutex> l(m_);
    watched_ = guids;
}

std::vector<int> Reader::pressed(const std::string &guid) const
{
    std::lock_guard<std::mutex> l(m_);
    const auto it = pressed_.find(guid);
    return it == pressed_.end() ? std::vector<int>{} : it->second;
}

void Reader::capture(const std::string &guid)
{
    {
        std::lock_guard<std::mutex> l(m_);
        captureDev_ = guid;
        watched_.insert(guid);
    }
    captured_ = -1;
    capturing_ = true;
}

void Reader::run()
{
    IDirectInput8W *di = nullptr;
    if (FAILED(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
                                  reinterpret_cast<void **>(&di), nullptr)))
        return;
    struct Dev { Device info; IDirectInputDevice8W *d; };
    std::vector<Dev> devs;
    std::map<std::string, std::vector<int>> prev;
    bool wasCapturing = false;
    while (!stop_) {
        if (rescan_.exchange(false)) {
            for (auto &d : devs) { d.d->Unacquire(); d.d->Release(); }
            devs.clear();
            struct Ctx { IDirectInput8W *di; std::vector<Dev> *out; } ctx{di, &devs};
            di->EnumDevices(DI8DEVCLASS_GAMECTRL, [](LPCDIDEVICEINSTANCEW inst, LPVOID p) -> BOOL {
                auto *c = static_cast<Ctx *>(p);
                IDirectInputDevice8W *d = nullptr;
                if (FAILED(c->di->CreateDevice(inst->guidInstance, &d, nullptr))) return DIENUM_CONTINUE;
                if (FAILED(d->SetDataFormat(&c_dfDIJoystick2))) { d->Release(); return DIENUM_CONTINUE; }
                DIDEVCAPS caps{};
                caps.dwSize = sizeof caps;
                d->GetCapabilities(&caps);
                c->out->push_back({{guidText(inst->guidInstance), guidText(inst->guidProduct), utf8(inst->tszProductName), int(caps.dwButtons)}, d});
                return DIENUM_CONTINUE;
            }, &ctx, DIEDFL_ATTACHEDONLY);
            std::lock_guard<std::mutex> l(m_);
            devices_.clear();
            for (auto &d : devs) devices_.push_back(d.info);
        }
        std::set<std::string> watched;
        std::string capDev;
        {
            std::lock_guard<std::mutex> l(m_);
            watched = watched_;
            capDev = captureDev_;
        }
        const bool cap = capturing_;
        // a new capture starts from what is held now: only a press after Bind counts
        const bool freshCapture = cap && !wasCapturing;
        wasCapturing = cap;
        std::map<std::string, std::vector<int>> now;
        for (auto &d : devs) {
            if (!watched.count(d.info.guid)) continue;
            DIJOYSTATE2 st;
            if (FAILED(d.d->Poll()) && (FAILED(d.d->Acquire()) || FAILED(d.d->Poll()))) continue;
            if (FAILED(d.d->GetDeviceState(sizeof st, &st))) continue;
            auto &v = now[d.info.guid];
            for (int i = 0; i < 128; i++)
                if (st.rgbButtons[i] & 0x80) v.push_back(i + 1);
        }
        if (cap && !freshCapture) {
            const auto &p = prev[capDev];
            for (int b : now[capDev])
                if (std::find(p.begin(), p.end(), b) == p.end()) { captured_ = b; capturing_ = false; break; }
        }
        prev = now;
        {
            std::lock_guard<std::mutex> l(m_);
            pressed_ = now;
        }
        Sleep(10);
    }
    for (auto &d : devs) { d.d->Unacquire(); d.d->Release(); }
    di->Release();
}

} // namespace wheel
