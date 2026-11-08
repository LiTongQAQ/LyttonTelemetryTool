// One program at a time: two copies would share UDP port 20700 (each getting part of the packets) and fight over the
// SteamVR overlays. The new and the old window (LyttonClassic) share the lock. When a copy is already running its
// window is brought to the front and this one should exit.
#pragma once

#include <windows.h>

#include <cwchar>

namespace single {

inline BOOL CALLBACK raiseLytton(HWND h, LPARAM)
{
    wchar_t title[256];
    if (!IsWindowVisible(h) || !GetWindowTextW(h, title, 256)) return TRUE;
    if (std::wcsncmp(title, L"Lytton Telemetry Tool", 21) != 0) return TRUE;
    if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
    SetForegroundWindow(h);
    return FALSE;
}

// true = this is the only copy (keep the returned lock for the program's lifetime)
inline bool acquire()
{
    static HANDLE lock = CreateMutexW(nullptr, TRUE, L"Local\\LyttonTelemetryTool.single");
    if (lock && GetLastError() == ERROR_ALREADY_EXISTS) {
        EnumWindows(raiseLytton, 0);
        return false;
    }
    return true;
}

} // namespace single
