#pragma once
#include "../pch.h"
#include "../Network/EpyksClient.h"

// AppState bridges EpyksClient events → WinUI DispatcherQueue callbacks
// so that ViewModels can safely update XAML-bound data on the UI thread.

namespace epyks_winui
{
    class AudioClient;

    // --- Observable data containers (plain C++, refreshed each update) ---
    struct AppState
    {
        // Auth
        std::string username;
        std::string sessionToken;
        std::string serverAddress; // host:port saved across sessions
        bool isLoggedIn = false;
        bool rememberMe = true;    // default to true
        HWND mainWindowHwnd = nullptr; // stored on startup for FileOpenPicker
        epyks::UserProfile myProfile;

        // Servers + channels + messages
        std::map<int, ServerModel>  servers;   // id -> server
        std::vector<FriendEntry>    friends;
        std::vector<std::string>    friendRequests;
        std::map<std::string, DMChat> dmChats;
        std::map<std::string, epyks::UserProfile> profileCache;

        // Selection
        int  currentServerId  = -1;
        int  currentChannelId = -1;
        std::string currentDM;

        // Voice
        bool inVoice = false;
        int  voiceServerId  = -1;
        int  voiceChannelId = -1;
        bool isMuted    = false;
        bool isDeafened = false;
        std::string audioInDevice;
        std::string audioOutDevice;
        std::map<std::string, uint64_t> voiceActivity; // username -> last seen timestamp (ms)

        // UI state
        std::string lastOpStatus;
        std::string pendingProfileUser;

        // Callbacks (set by UI components to react to changes)
        std::function<void()> OnStateChanged;

        // Dispatcher for marshalling to UI thread
        winrt::Microsoft::UI::Dispatching::DispatcherQueue uiDispatcher{ nullptr };

        void FireChanged()
        {
            if (uiDispatcher && OnStateChanged)
            {
                uiDispatcher.TryEnqueue([this]() {
                    if (OnStateChanged) OnStateChanged();
                });
            }
        }

        // Helpers
        ServerModel* GetCurrentServer()
        {
            auto it = servers.find(currentServerId);
            return (it != servers.end()) ? &it->second : nullptr;
        }
        std::vector<MsgModel>* GetCurrentMessages()
        {
            auto* srv = GetCurrentServer();
            if (!srv) return nullptr;
            auto it = srv->channelMessages.find(currentChannelId);
            return (it != srv->channelMessages.end()) ? &it->second : nullptr;
        }
    };

    // Singleton access
    AppState& GetAppState();
    class AudioClient;
    AudioClient& GetAudioClient();

    // Call once at startup to wire EpyksClient callbacks -> AppState
    void InitAppState(winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher);

    // Config helpers
    void SaveConfig(const std::string& user, const std::string& token,
                    const std::string& inDev = "", const std::string& outDev = "",
                    const std::string& server = "");
    bool LoadConfig(std::string& user, std::string& token,
                    std::string& inDev, std::string& outDev,
                    std::string& server);
    void ClearConfig();
    std::string GetAppDataPath(const std::string& filename);
    std::string GetCachePath(const std::string& filename); // for pfp/media cache
    winrt::Microsoft::UI::Xaml::Media::Brush GetAvatarBrush(const std::string& username);
}
