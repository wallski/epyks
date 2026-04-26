#include "ServerGUI.h"
#include "Database.h"


extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;

    switch (msg) {
    case WM_SIZE:
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

bool ServerGUI::Initialize() {
    WNDCLASSEX wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, _T("EpyksServer"), nullptr };
    RegisterClassEx(&wc);
    hwnd = CreateWindow(wc.lpszClassName, _T("Epyks Server"), WS_OVERLAPPEDWINDOW, 100, 100, 1000, 700, nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDevice()) {
        CleanupDevice();
        return false;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    config.Apply();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(pd3dDevice, pd3dDeviceContext);

    AddLog("[System] Epyks Server GUI initialized");
    AddLog("[System] Ready to start. Configure port in Settings or use default 9001.");

    return true;
}

void ServerGUI::Shutdown() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDevice();
    DestroyWindow(hwnd);
    UnregisterClass(_T("EpyksServer"), GetModuleHandle(nullptr));
}

bool ServerGUI::RunFrame() {
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (msg.message == WM_QUIT) return false;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    DrawMainWindow();
    DrawSettingsPopup();

    ImGui::Render();
    const float clear_color[4] = { config.bgColor.x, config.bgColor.y, config.bgColor.z, config.bgColor.w };
    pd3dDeviceContext->OMSetRenderTargets(1, &mainRenderTargetView, nullptr);
    pd3dDeviceContext->ClearRenderTargetView(mainRenderTargetView, clear_color);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    pSwapChain->Present(1, 0);

    return true;
}

void ServerGUI::DrawMainWindow() {
    ImVec2 viewportSize = ImGui::GetMainViewport()->Size;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(viewportSize);
    ImGui::Begin("Epyks Server", nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_MenuBar |
        ImGuiWindowFlags_NoBringToFrontOnFocus);


    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("Server")) {
            if (ImGui::MenuItem("Start")) doStart = true;
            if (ImGui::MenuItem("Stop")) doStop = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Clear History")) {
                if (database) database->ClearChatHistory();

                {
                    std::lock_guard<std::mutex> lock(logMutex);
                    logs.clear();
                }

                AddLog("[System] Chat history cleared (database wiped)");
                scrollToBottom = true;
            }

            if (ImGui::MenuItem("Manage Servers...")) showGroupManager = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) PostQuitMessage(0);
            ImGui::EndMenu();
        }

        

        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Auto-scroll", nullptr, &autoScroll);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Settings")) {
            if (ImGui::MenuItem("Preferences...")) showSettings = true;
            ImGui::EndMenu();
        }

        if (showGroupManager) ImGui::OpenPopup("Manage Servers");
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_Always);
        if (ImGui::BeginPopupModal("Manage Servers", &showGroupManager, ImGuiWindowFlags_NoResize)) {
            ImGui::Text("All Servers:");
            ImGui::Separator();
            if (database) {
                auto servers_list = database->GetAllServers();
                if (servers_list.empty()) {
                    ImGui::Text("No servers exist.");
                }
                for (auto& s : servers_list) {
                    int sid = std::get<0>(s);
                    std::string& sname = std::get<1>(s);
                    bool hasPass = std::get<2>(s);
                    ImGui::Text("[%d] %s%s", sid, sname.c_str(), hasPass ? " (Locked)" : "");
                    ImGui::SameLine();
                    if (ImGui::Button(("Delete##" + std::to_string(sid)).c_str())) {
                        database->DeleteServer(sid);
                        AddLog("[System] Deleted server: " + sname);
                    }
                }
            }
            ImGui::Separator();
            if (ImGui::Button("Close", ImVec2(120, 0))) {
                showGroupManager = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::EndMenuBar();
    }


    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
    ImGui::BeginChild("StatusBar", ImVec2(0, 45), true, ImGuiWindowFlags_NoScrollbar);

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Status:");
    ImGui::SameLine();

    if (serverRunning)
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.3f, 1.0f), "ONLINE");
    else
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "OFFLINE");

    ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();
    ImGui::Text("Port: %d", config.port);
    ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();
    ImGui::Text("Clients: %d/%d", clientCount, config.maxClients);

    ImGui::SameLine(); ImGui::Dummy(ImVec2(20, 0)); ImGui::SameLine();

    if (!serverRunning) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.7f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.8f, 0.4f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.6f, 0.2f, 1.0f));
        if (ImGui::Button("START SERVER", ImVec2(140, 30))) doStart = true;
        ImGui::PopStyleColor(3);
    }
    else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
        if (ImGui::Button("STOP SERVER", ImVec2(140, 30))) doStop = true;
        ImGui::PopStyleColor(3);
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear Log", ImVec2(100, 30))) {
        std::lock_guard<std::mutex> lock(logMutex);
        logs.clear();
        scrollToBottom = true;
    }

    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &autoScroll);

    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Separator();


    float logHeight = viewportSize.y - 110;
    ImGui::BeginChild("ServerLog", ImVec2(0, logHeight), true, ImGuiWindowFlags_HorizontalScrollbar);

    std::vector<std::string> logsCopy;
    {
        std::lock_guard<std::mutex> lock(logMutex);
        logsCopy = logs;
    }

    for (size_t i = 0; i < logsCopy.size(); i++) {
        const char* item = logsCopy[i].c_str();
        ImVec4 color = config.textColor;

        if (strstr(item, "[System]")) color = ImVec4(0.5f, 0.8f, 1.0f, 1.0f);
        else if (strstr(item, "joined")) color = ImVec4(0.5f, 1.0f, 0.5f, 1.0f);
        else if (strstr(item, "left")) color = ImVec4(1.0f, 0.5f, 0.5f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::Selectable(item, false);
        ImGui::PopStyleColor();

        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1)) {
            ImGui::SetClipboardText(item);
        }
    }


    if (autoScroll || scrollToBottom) {
        ImGui::SetScrollHereY(1.0f);
        scrollToBottom = false;
    }

    ImGui::EndChild();


    ImGui::Separator();
    ImGui::Text("Right-click log entry to copy");

    ImGui::End();
}


void ServerGUI::DrawSettingsPopup() {
    if (showSettings) ImGui::OpenPopup("Server Settings");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(450, 400), ImGuiCond_Always);

    if (ImGui::BeginPopupModal("Server Settings", &showSettings, ImGuiWindowFlags_NoResize)) {
        ImGui::Text("Server Configuration");
        ImGui::Separator();

        ImGui::InputInt("Port", &config.port);
        if (config.port < 1024) config.port = 1024;
        if (config.port > 65535) config.port = 65535;

        ImGui::InputInt("Max Clients", &config.maxClients);
        if (config.maxClients < 1) config.maxClients = 1;
        if (config.maxClients > 1000) config.maxClients = 1000;

        ImGui::Checkbox("Auto-start on launch", &config.autoStart);

        ImGui::Separator();
        ImGui::Text("Appearance");

        ImGui::ColorEdit4("Background", (float*)&config.bgColor);
        ImGui::ColorEdit4("Log Background", (float*)&config.logColor);
        ImGui::ColorEdit4("Text Color", (float*)&config.textColor);

        ImGui::Separator();

        float buttonWidth = 120;
        float windowWidth = ImGui::GetWindowWidth();
        float startPos = (windowWidth - buttonWidth * 3 - 20) * 0.5f;

        ImGui::SetCursorPosX(startPos);
        if (ImGui::Button("Apply", ImVec2(buttonWidth, 0))) {
            config.Apply();
        }

        ImGui::SameLine();
        if (ImGui::Button("Reset", ImVec2(buttonWidth, 0))) {
            config = ServerConfig();
            config.Apply();
        }

        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(buttonWidth, 0))) {
            showSettings = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void ServerGUI::AddLog(const std::string& msg) {
    std::lock_guard<std::mutex> lock(logMutex);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char timeStr[32];
    sprintf_s(timeStr, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);

    logs.push_back(std::string(timeStr) + msg);

    if (logs.size() > 10000) {
        logs.erase(logs.begin(), logs.begin() + 1000);
    }
}

bool ServerGUI::ShouldStartServer() {
    return doStart;
}

bool ServerGUI::ShouldStopServer() {
    return doStop;
}

void ServerGUI::ServerStarted() {
    serverRunning = true;
    doStart = false;
    AddLog("[System] Server started successfully on port " + std::to_string(config.port));
}

void ServerGUI::ServerStopped() {
    serverRunning = false;
    doStop = false;
    clientCount = 0;
    AddLog("[System] Server stopped");
}

bool ServerGUI::CreateDevice() {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        featureLevelArray, 2, D3D11_SDK_VERSION,
        &sd, &pSwapChain, &pd3dDevice, &featureLevel, &pd3dDeviceContext
    );

    if (FAILED(hr)) return false;

    CreateRenderTarget();
    return true;
}

void ServerGUI::CleanupDevice() {
    CleanupRenderTarget();
    if (pSwapChain) { pSwapChain->Release(); pSwapChain = nullptr; }
    if (pd3dDeviceContext) { pd3dDeviceContext->Release(); pd3dDeviceContext = nullptr; }
    if (pd3dDevice) { pd3dDevice->Release(); pd3dDevice = nullptr; }
}

void ServerGUI::CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &mainRenderTargetView);
        pBackBuffer->Release();
    }
}

void ServerGUI::CleanupRenderTarget() {
    if (mainRenderTargetView) { mainRenderTargetView->Release(); mainRenderTargetView = nullptr; }
}