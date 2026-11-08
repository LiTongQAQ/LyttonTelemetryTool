#include "log.hpp"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <mutex>

namespace applog {

namespace {
std::mutex g_mu;
FILE *g_file = nullptr;
std::string g_path;
thread_local bool t_hold = false; // only the holding thread's lines are buffered
thread_local std::string t_held;
} // namespace

void open(const std::string &dir)
{
    namespace fs = std::filesystem;
    std::lock_guard lk(g_mu);
    if (g_file) return;
    const fs::path p = fs::path(dir) / "lytton.log", prev = fs::path(dir) / "lytton.previous.log";
    std::error_code ec;
    if (fs::exists(p, ec)) fs::rename(p, prev, ec);
    g_file = _wfopen(p.wstring().c_str(), L"wb");
    if (g_file) std::fputs("\xEF\xBB\xBF", g_file); // UTF-8 BOM: Notepad shows the Chinese text correctly
    g_path = p.string();
}

void close()
{
    std::lock_guard lk(g_mu);
    if (g_file) std::fclose(g_file);
    g_file = nullptr;
}

std::string path()
{
    std::lock_guard lk(g_mu);
    return g_path;
}

void write(const char *fmt, ...)
{
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    SYSTEMTIME t;
    GetLocalTime(&t);
    char line[2200];
    std::snprintf(line, sizeof line, "%04d-%02d-%02d %02d:%02d:%02d.%03d  %s\r\n", t.wYear, t.wMonth, t.wDay, t.wHour,
                  t.wMinute, t.wSecond, t.wMilliseconds, msg);
    if (t_hold) { t_held += line; return; }
    std::lock_guard lk(g_mu);
    if (!g_file) return;
    std::fputs(line, g_file);
    std::fflush(g_file);
}

void hold()
{
    t_hold = true;
    t_held.clear();
}

void release(bool keep)
{
    std::lock_guard lk(g_mu);
    if (keep && g_file && !t_held.empty()) {
        std::fputs(t_held.c_str(), g_file);
        std::fflush(g_file);
    }
    t_hold = false;
    t_held.clear();
}

} // namespace applog
