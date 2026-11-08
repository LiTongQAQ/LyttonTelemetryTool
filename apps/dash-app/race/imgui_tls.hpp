// ImGui user config (IMGUI_USER_CONFIG): the current ImGui context is per thread, so the HMD panels can render on
// their own thread (apps/dash-qt engine) while the desktop canvases use their contexts on the GUI thread.
#pragma once
struct ImGuiContext;
extern thread_local ImGuiContext *g_imguiTls;
#define GImGui g_imguiTls
