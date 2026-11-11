#include "launcher.hpp"

#include "log.hpp"

#include <filesystem>
#include <functional>
#include <sstream>

#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

namespace fs = std::filesystem;

namespace launcher {

namespace {

std::wstring widen(const std::string &s) { return fs::u8path(s).wstring(); }
std::string narrow(const std::wstring &w) { return fs::path(w).u8string(); }

bool processRunning(const wchar_t *name)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W e{sizeof e};
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &e); ok && !found; ok = Process32NextW(snap, &e))
        found = _wcsicmp(e.szExeFile, name) == 0;
    CloseHandle(snap);
    return found;
}

// Runs a console program without a window and returns what it printed (stdout and stderr) and its exit code.
int run(const std::string &exe, const std::string &args, std::string &out, DWORD timeoutMs = 120000)
{
    out.clear();
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{sizeof si};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + widen(exe) + L"\" " + widen(args);
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(rd);
        CloseHandle(wr);
        return -1;
    }
    CloseHandle(wr);
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(rd, buf, sizeof buf, &n, nullptr) && n) out.append(buf, n);
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, timeoutMs);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return int(code);
}

// Starts a program that keeps running (the game session) without a window; its output goes to `logFile`.
bool spawn(const std::string &exe, const std::string &args, const std::string &logFile)
{
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE log = CreateFileW(widen(logFile).c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{sizeof si};
    if (log != INVALID_HANDLE_VALUE) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = si.hStdError = log;
    }
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + widen(exe) + L"\" " + widen(args);
    const bool ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | DETACHED_PROCESS,
                                   nullptr, nullptr, &si, &pi);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    if (ok) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    return ok;
}

std::vector<std::string> lines(const std::string &s)
{
    std::vector<std::string> v;
    std::istringstream in(s);
    for (std::string l; std::getline(in, l);) {
        while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
        if (!l.empty()) v.push_back(l);
    }
    return v;
}

// columns separated by two or more spaces ("ross.wheel-dash   0.11.0   enabled   Wheel Dash")
std::vector<std::string> columns(const std::string &l)
{
    std::vector<std::string> c;
    size_t i = 0;
    while (i < l.size()) {
        while (i < l.size() && l[i] == ' ') i++;
        if (i >= l.size()) break;
        size_t j = i;
        while (j < l.size() && !(l[j] == ' ' && j + 1 < l.size() && l[j + 1] == ' ')) j++;
        c.push_back(l.substr(i, j - i));
        i = j;
    }
    return c;
}

std::string quote(const std::string &s) { return "\"" + s + "\""; }

std::string localAppData()
{
    wchar_t buf[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    return n && n < MAX_PATH ? narrow(buf) : std::string();
}

std::string dataDir()
{
    const std::string d = localAppData();
    return d.empty() ? d : d + "\\LyttonTelemetryTool";
}

} // namespace

std::string findSwofty()
{
    wchar_t buf[MAX_PATH];
    if (SearchPathW(nullptr, L"swofty.exe", nullptr, MAX_PATH, buf, nullptr)) return narrow(buf);
    const std::string base = localAppData();
    const std::string guess = base.empty() ? base : base + "\\Programs\\SwoftyLauncher\\bin\\swofty.exe";
    std::error_code ec;
    return !guess.empty() && fs::exists(fs::u8path(guess), ec) ? guess : std::string();
}

Launcher::~Launcher()
{
    if (th_.joinable()) th_.join();
}

void Launcher::work(std::function<void()> fn)
{
    {
        std::lock_guard<std::mutex> l(m_);
        if (busy_) {
            msg_ = "Busy, try again in a moment";
            return;
        }
        busy_ = true;
    }
    if (th_.joinable()) th_.join();
    th_ = std::thread([this, fn] {
        fn();
        std::lock_guard<std::mutex> l(m_);
        busy_ = false;
    });
}

void Launcher::say(const std::string &m)
{
    std::lock_guard<std::mutex> l(m_);
    msg_ = m;
    LOG("launcher: %s", m.c_str());
}

Status Launcher::status() const
{
    std::lock_guard<std::mutex> l(m_);
    return st_;
}
bool Launcher::busy() const
{
    std::lock_guard<std::mutex> l(m_);
    return busy_;
}
std::string Launcher::message() const
{
    std::lock_guard<std::mutex> l(m_);
    return msg_;
}

void Launcher::doRefresh(const std::string &profile)
{
    Status s;
    s.checked = true;
    s.gameRunning = processRunning(L"F1_25.exe");
    s.exe = findSwofty();
    s.found = !s.exe.empty();
    if (s.found) {
        std::string out;
        run(s.exe, "--version", out);
        s.version = lines(out).empty() ? "" : lines(out)[0];
        run(s.exe, "profile list", out);
        for (const auto &l : lines(out)) {
            const auto c = columns(l);
            if (c.empty()) continue;
            s.profiles.push_back(c[0]);
            if (c.size() > 1 && c[1].find("active") != std::string::npos) s.activeProfile = c[0];
        }
        s.addonsProfile = profile.empty() ? s.activeProfile : profile;
        run(s.exe, "addons list" + (s.addonsProfile.empty() ? std::string() : " --profile " + quote(s.addonsProfile)), out);
        for (const auto &l : lines(out)) {
            const auto c = columns(l);
            if (c.size() >= 3) s.addons.push_back({c[0], c[1], c[2], c.size() > 3 ? c[3] : c[0]});
        }
        run(s.exe, "shaders list" + (s.addonsProfile.empty() ? std::string() : " --profile " + quote(s.addonsProfile)), out);
        for (const auto &l : lines(out)) {
            if (l.rfind("shaders:", 0) == 0) {
                s.activeShader = l.substr(8);
                while (!s.activeShader.empty() && s.activeShader.front() == ' ') s.activeShader.erase(0, 1);
                continue;
            }
            auto c = columns(l);
            if (c.size() < 2) continue;
            std::string id = c[0];
            const std::string mark = " active"; // "<id> active  <name>" for the chosen one
            if (id.size() > mark.size() && id.compare(id.size() - mark.size(), mark.size(), mark) == 0) id.resize(id.size() - mark.size());
            s.shaders.push_back({id, c[1]});
        }
        run(s.exe, "doctor", out);
        for (const auto &l : lines(out)) {
            const auto c = columns(l);
            if (c.size() < 2) continue;
            if (c[0] == "baseline") s.vanilla = c[1].find("no drift") != std::string::npos;
            else if (c[0] == "game dir") s.gameDir = c[1];
            else if (c[0] == "build id") s.gameBuild = c[1];
        }
        run(s.exe, "offline status", out);
        for (const auto &l : lines(out)) {
            if (l.rfind("consent:", 0) == 0) s.consent = l.find("not") == std::string::npos;
            else if (l.rfind("extension:", 0) == 0) s.extension = l.find("not usable") == std::string::npos && l.find("missing") == std::string::npos;
        }
    }
    std::lock_guard<std::mutex> l(m_);
    st_ = s;
}

void Launcher::refresh(const std::string &profile)
{
    work([this, profile] { doRefresh(profile); });
}

void Launcher::setAddon(const std::string &profile, const std::string &id, bool on)
{
    work([this, profile, id, on] {
        const std::string exe = findSwofty();
        std::string out;
        const int rc = run(exe, std::string(on ? "addons enable " : "addons disable ") + quote(id) + " --profile " + quote(profile), out);
        say(rc == 0 ? std::string(on ? "Enabled " : "Disabled ") + id + " in " + profile : "Swofty: " + (lines(out).empty() ? "failed" : lines(out).back()));
        doRefresh(profile);
    });
}

void Launcher::setShader(const std::string &profile, const std::string &id)
{
    work([this, profile, id] {
        std::string out;
        const int rc = run(findSwofty(), "shaders use " + quote(id) + " --profile " + quote(profile), out);
        say(rc == 0 ? "Shader config for " + profile + ": " + id : "Swofty: " + (lines(out).empty() ? "failed" : lines(out).back()));
        doRefresh(profile);
    });
}

void Launcher::playOnline(bool vr)
{
    work([this, vr] {
        doRefresh("");
        const Status s = status();
        if (s.gameRunning) return say("F1 25 is already running");
        // a modded session that did not end cleanly leaves the folder changed: never go online like that
        if (s.found && !s.vanilla) return say("The game folder is not stock (a modded session left changes): press Restore first");
        const std::wstring url = vr ? L"steam://launch/" + widen(STEAM_APP_ID) + L"/VR"
                                    : L"steam://rungameid/" + widen(STEAM_APP_ID);
        const auto h = reinterpret_cast<intptr_t>(ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        say(h > 32 ? std::string("Starting F1 25 through Steam") + (vr ? " in VR" : "") + " (online, stock game)"
                   : "Could not ask Steam to start the game");
    });
}

void Launcher::playOffline(const std::string &profile, bool vr)
{
    work([this, profile, vr] {
        doRefresh(profile);
        const Status s = status();
        if (!s.found) return say("Swofty Launcher not found: install it from f1.swofty.net");
        if (s.gameRunning) return say("F1 25 is already running");
        if (!s.consent || !s.extension)
            return say("Offline play needs Swofty's one-time setup: open Swofty Launcher and press Play once (accept the terms)");
        std::error_code ec;
        const std::string dir = dataDir();
        fs::create_directories(fs::u8path(dir), ec);
        const std::string args = "run --apply --profile " + quote(profile) + (vr ? " --vr" : "");
        const bool ok = spawn(s.exe, args, dir + "\\swofty-run.log");
        say(ok ? "Starting F1 25 offline with profile " + profile + (vr ? " in VR" : "") + " (Swofty log: swofty-run.log)"
               : "Could not start Swofty");
    });
}

void Launcher::restore()
{
    work([this] {
        const std::string exe = findSwofty();
        if (exe.empty()) return say("Swofty Launcher not found");
        if (processRunning(L"F1_25.exe")) return say("Close F1 25 first");
        std::string out;
        const int rc = run(exe, "restore", out, 600000);
        say(rc == 0 ? "Game folder restored to stock" : "Swofty restore: " + (lines(out).empty() ? "failed" : lines(out).back()));
        doRefresh("");
    });
}

void Launcher::openSwofty() const
{
    const std::string exe = findSwofty();
    if (exe.empty()) return;
    const fs::path gui = fs::u8path(exe).parent_path() / L"swofty-launcher.exe";
    ShellExecuteW(nullptr, L"open", gui.wstring().c_str(), nullptr, gui.parent_path().wstring().c_str(), SW_SHOWNORMAL);
}

} // namespace launcher
