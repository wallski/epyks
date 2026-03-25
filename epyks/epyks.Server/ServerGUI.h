#pragma once
#include <windows.h>
#include <d3d11.h>
#include <tchar.h>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <vector>
#include <string>
#include <atomic>
#include <mutex>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

class Database;

struct ServerConfig {
    int port = 9001;
    int maxClients = 100;
    bool autoStart = false;
    ImVec4 bgColor = ImVec4(0.07f, 0.05f, 0.11f, 1.0f);
    ImVec4 logColor = ImVec4(0.10f, 0.07f, 0.16f, 1.0f);
    ImVec4 textColor = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);

    void Apply() {
        ImGuiStyle& style = ImGui::GetStyle();

        style.WindowRounding = 8.0f;
        style.ChildRounding = 6.0f;
        style.FrameRounding = 5.0f;
        style.PopupRounding = 6.0f;
        style.ScrollbarRounding = 4.0f;
        style.GrabRounding = 4.0f;
        style.TabRounding = 4.0f;
        style.WindowPadding = ImVec2(12, 12);
        style.FramePadding = ImVec2(8, 4);
        style.ItemSpacing = ImVec2(8, 6);
        style.ScrollbarSize = 10.0f;

        style.Colors[ImGuiCol_WindowBg] = bgColor;
        style.Colors[ImGuiCol_ChildBg] = logColor;
        style.Colors[ImGuiCol_Text] = textColor;
        style.Colors[ImGuiCol_Button] = ImVec4(0.35f, 0.10f, 0.60f, 1.0f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.45f, 0.20f, 0.75f, 1.0f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.25f, 0.05f, 0.50f, 1.0f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.10f, 0.22f, 1.0f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.14f, 0.30f, 1.0f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.18f, 0.38f, 1.0f);
        style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.08f, 0.05f, 0.13f, 1.0f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.35f, 0.10f, 0.60f, 0.5f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.45f, 0.20f, 0.75f, 0.7f);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.45f, 0.20f, 0.75f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.08f, 0.05f, 0.13f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.35f, 0.10f, 0.60f, 0.6f);
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.45f, 0.20f, 0.75f, 0.8f);
        style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.45f, 0.20f, 0.75f, 1.0f);
        style.Colors[ImGuiCol_Separator] = ImVec4(0.35f, 0.10f, 0.60f, 0.4f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.05f, 0.13f, 1.0f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.18f, 0.08f, 0.32f, 1.0f);
        style.Colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.07f, 0.16f, 1.0f);
    }
};

class ServerGUI {
public:
    bool Initialize();
    void Shutdown();
    bool RunFrame();

    void SetDatabase(Database* db) { database = db; }
   
    bool scrollToBottom = false;
    void AddLog(const std::string& msg);
    bool ShouldStartServer();
    bool ShouldStopServer();
    int GetPort() { return config.port; }
    void ServerStarted();
    void ServerStopped();
    bool IsServerRunning() { return serverRunning; }
    void ClearFlags() { doStart = false; doStop = false; }

private:
    HWND hwnd = nullptr;
    ID3D11Device* pd3dDevice = nullptr;
    ID3D11DeviceContext* pd3dDeviceContext = nullptr;
    IDXGISwapChain* pSwapChain = nullptr;
    ID3D11RenderTargetView* mainRenderTargetView = nullptr;

    ServerConfig config;
    std::vector<std::string> logs;
    std::mutex logMutex;
    std::atomic<bool> doStart = false;
    std::atomic<bool> doStop = false;
    bool serverRunning = false;
    bool showSettings = false;
    bool autoScroll = true;
    int clientCount = 0;
 
    bool showGroupManager = false;

    Database* database = nullptr;

    bool CreateDevice();
    void CleanupDevice();
    void CreateRenderTarget();
    void CleanupRenderTarget();
    void DrawMainWindow();
    void DrawSettingsPopup();
};