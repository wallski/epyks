#include "pch.h"
#include "AppState.h"
#include "../Network/EpyksClient.h"
#include "../AudioClient.h"

namespace epyks_winui
{
    static AppState g_state;
    AppState& GetAppState() { return g_state; }

    static AudioClient g_audio;
    AudioClient& GetAudioClient() { return g_audio; }

    // ---------------------------------------------------------------
    // Config helpers
    // ---------------------------------------------------------------
    std::string GetAppDataPath(const std::string& filename)
    {
        char path[MAX_PATH];
        GetEnvironmentVariableA("APPDATA", path, MAX_PATH);
        std::string dir = std::string(path) + "\\Epyks";
        std::filesystem::create_directories(dir);
        return dir + "\\" + filename;
    }

    std::string GetCachePath(const std::string& filename)
    {
        char path[MAX_PATH];
        GetEnvironmentVariableA("APPDATA", path, MAX_PATH);
        std::string dir = std::string(path) + "\\Epyks\\cache";
        std::filesystem::create_directories(dir);
        return dir + "\\" + filename;
    }

    winrt::Microsoft::UI::Xaml::Media::Brush GetAvatarBrush(const std::string& username)
    {
        std::string cachePath = GetCachePath(username + ".png");
        if (std::filesystem::exists(cachePath))
        {
            auto uri = winrt::Windows::Foundation::Uri(winrt::to_hstring("file:///" + cachePath));
            auto bmp = winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage();
            bmp.CreateOptions(winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapCreateOptions::IgnoreImageCache);
            bmp.UriSource(uri);
            auto brush = winrt::Microsoft::UI::Xaml::Media::ImageBrush();
            brush.ImageSource(bmp);
            brush.Stretch(winrt::Microsoft::UI::Xaml::Media::Stretch::UniformToFill);
            return brush;
        }
        size_t h = std::hash<std::string>{}(username);
        uint8_t r = 80 + (h & 0x7F);
        uint8_t g = 80 + ((h >> 8) & 0x7F);
        uint8_t b = 180 + ((h >> 16) & 0x3F);
        return winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Microsoft::UI::ColorHelper::FromArgb(255, r, g, b));
    }

    void SaveConfig(const std::string& user, const std::string& token,
                    const std::string& inDev, const std::string& outDev,
                    const std::string& server)
    {
        std::ofstream f(GetAppDataPath("config.ini"));
        if (f) f << user << "\n" << token << "\n" << inDev << "\n" << outDev << "\n" << server << "\n";
    }

    bool LoadConfig(std::string& user, std::string& token,
                    std::string& inDev, std::string& outDev,
                    std::string& server)
    {
        std::ifstream f(GetAppDataPath("config.ini"));
        if (!f) return false;
        std::getline(f, user);
        std::getline(f, token);
        std::getline(f, inDev);
        std::getline(f, outDev);
        std::getline(f, server);
        
        return (!user.empty() && !token.empty()); // Returns true if auto-login is possible
    }

    void ClearConfig() 
    { 
        std::string user, token, inDev, outDev, server;
        LoadConfig(user, token, inDev, outDev, server);
        SaveConfig("", "", inDev, outDev, server);
    }

    // ---------------------------------------------------------------
    // Wire EpyksClient events -> AppState
    // ---------------------------------------------------------------
    void InitAppState(winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher)
    {
        g_state.uiDispatcher = dispatcher;
        auto& client = GetClient();
        
        // Init audio
        g_audio.Initialize();

        // Login
        client.OnLoginResult = [dispatcher](bool ok, std::string err, std::string token)
        {
            dispatcher.TryEnqueue([=]() {
                auto& s = GetAppState();
                if (ok)
                {
                    s.isLoggedIn  = true;
                    s.sessionToken = token;
                    if (s.rememberMe)
                        SaveConfig(s.username, token, s.audioInDevice, s.audioOutDevice, s.serverAddress);
                    else
                        SaveConfig("", "", s.audioInDevice, s.audioOutDevice, s.serverAddress);
                }
                else
                {
                    s.lastOpStatus = "Login failed: " + err;
                }
                s.FireChanged();
            });
        };

        client.OnRegisterResult = [dispatcher](bool ok, std::string err)
        {
            dispatcher.TryEnqueue([=]() {
                auto& s = GetAppState();
                s.lastOpStatus = ok ? "REGISTER_SUCCESS" : ("Register failed: " + err);
                s.FireChanged();
            });
        };

        // Server messages
        client.OnServerMessage = [dispatcher](int sid, int cid, MsgModel msg)
        {
            dispatcher.TryEnqueue([=]() {
                auto& s = GetAppState();
                s.servers[sid].channelMessages[cid].push_back(msg);
                s.FireChanged();
            });
        };

        // Private messages
        client.OnPrivateMessage = [dispatcher](std::string partner, MsgModel msg)
        {
            dispatcher.TryEnqueue([=]() {
                auto& s = GetAppState();
                s.dmChats[partner].username = partner;
                s.dmChats[partner].messages.push_back(msg);
                s.FireChanged();
            });
        };

        // Friend list
        client.OnFriendList = [dispatcher](std::vector<FriendEntry> friends)
        {
            dispatcher.TryEnqueue([=]() {
                auto& s = GetAppState();
                s.friends = friends;
                s.FireChanged();
            });
        };

        client.OnFriendRequest = [dispatcher](std::string from)
        {
            dispatcher.TryEnqueue([=]() {
                auto& s = GetAppState();
                if (from.find("wants to add you") != std::string::npos) return;
                s.friendRequests.push_back(from);
                s.FireChanged();
            });
        };

        // My servers
        client.OnMyServers = [dispatcher](std::map<int, ServerModel> servers)
        {
            dispatcher.TryEnqueue([=]() mutable {
                auto& s = GetAppState();
                for (auto& [id, srv] : servers)
                {
                    if (s.servers.count(id))
                    {
                        srv.channels       = s.servers[id].channels;
                        srv.channelMessages = s.servers[id].channelMessages;
                    }
                }
                s.servers = servers;
                s.FireChanged();
            });
        };

        client.OnJoinServer = [dispatcher](int id)
        {
            dispatcher.TryEnqueue([=]() {
                auto& s = GetAppState();
                s.currentServerId = id;
                s.currentChannelId = -1;
                GetClient().RequestMyServers();
                GetClient().RequestChannelList(id);
                GetClient().RequestMemberList(id);
                s.FireChanged();
            });
        };

        client.OnChannelList = [dispatcher](int serverId, std::vector<ChannelModel> channels)
        {
            dispatcher.TryEnqueue([=]() {
                auto& s = GetAppState();
                s.servers[serverId].channels = channels;

                if (s.currentServerId == serverId && s.currentChannelId == -1)
                {
                    for (auto& ch : channels)
                    {
                        if (ch.type == 0)
                        {
                            s.currentChannelId = ch.id;
                            s.servers[serverId].channelMessages[ch.id].clear();
                            GetClient().RequestServerHistory(serverId, ch.id);
                            break;
                        }
                    }
                }
                s.FireChanged();
            });
        };

        client.OnMemberList = [dispatcher](int serverId, std::vector<ServerMember> members)
        {
            dispatcher.TryEnqueue([=]() {
                auto& s = GetAppState();
                if (s.servers.count(serverId))
                    s.servers[serverId].members = members;
                s.FireChanged();
            });
        };

        client.OnProfileData = [dispatcher](epyks::UserProfile profile)
        {
            dispatcher.TryEnqueue([=]() {
                auto& s = GetAppState();
                s.profileCache[profile.username] = profile;
                if (profile.username == s.username) s.myProfile = profile;
                if (!profile.pfp_url.empty())
                {
                    std::string cacheFile = GetCachePath(profile.username + ".png");
                    if (!std::filesystem::exists(cacheFile))
                        GetClient().RequestMedia(profile.pfp_url);
                }
                s.FireChanged();
            });
        };

        client.OnPfpReceived = [dispatcher](std::string username, std::vector<uint8_t> data)
        {
            {
                std::string cachePath = GetCachePath(username + ".png");
                std::ofstream f(cachePath, std::ios::binary);
                if (f) f.write(reinterpret_cast<const char*>(data.data()), data.size());
            }
            
            dispatcher.TryEnqueue([=]() {
                GetAppState().FireChanged();
            });
        };

        client.OnMediaReceived = [dispatcher](std::string filename, std::vector<uint8_t> data)
        {
            if (filename.empty() || data.empty()) return;
            std::string cachePath = GetCachePath(filename);
            std::ofstream f(cachePath, std::ios::binary);
            if (f) f.write(reinterpret_cast<const char*>(data.data()), data.size());
            
            dispatcher.TryEnqueue([=]() {
                GetAppState().FireChanged();
            });
        };

        client.OnMediaUploadComplete = [dispatcher](std::string url)
        {
            dispatcher.TryEnqueue([=]() {
                auto& s = GetAppState();
                if (s.currentServerId != -1 && s.currentChannelId != -1)
                    GetClient().SendServerMessage(s.currentServerId, s.currentChannelId, url);
                else if (!s.currentDM.empty())
                    GetClient().SendPrivateMessage(s.currentDM, url);
            });
        };

        client.OnMyDms = [dispatcher](std::vector<std::string> dms)
        {
            dispatcher.TryEnqueue([=]() {
                auto& s = GetAppState();
                for (auto& u : dms) {
                    if (u.find("wants to add you") != std::string::npos) continue;
                    if (!s.dmChats.count(u)) s.dmChats[u] = { u };
                }
                s.FireChanged();
            });
        };

        client.OnAvailableServers = [](std::vector<std::tuple<int, std::string, bool>> list)
        {
            // Let the browsing dialog handle this directly via the callback
            (void)list;
        };

        client.OnOpStatus = [dispatcher](std::string status)
        {
            dispatcher.TryEnqueue([=]() {
                auto& s = GetAppState();
                s.lastOpStatus = status;
                GetClient().RequestMyServers();
                GetClient().RequestFriendList();
                GetClient().RequestMyDms();
                s.FireChanged();
            });
        };

        client.OnHistory = [dispatcher](int sid, int cid, std::vector<MsgModel> msgs)
        {
            dispatcher.TryEnqueue([=]() {
                auto& s = GetAppState();
                if (sid != -1) {
                    s.servers[sid].channelMessages[cid] = msgs;
                }
                s.FireChanged();
            });
        };

        client.OnVoiceData = [dispatcher](std::string username, std::vector<uint8_t> data)
        {
            g_audio.PushVoiceData(data);
            dispatcher.TryEnqueue([=]() {
                auto& s = GetAppState();
                s.voiceActivity[username] = GetTickCount64();
            });
        };

        // Outgoing audio pump
        std::thread([]() {
            while (true) {
                auto& s = GetAppState();
                auto& c = GetClient();
                if (s.isLoggedIn && s.inVoice && s.voiceServerId != -1) {
                    std::vector<uint8_t> data;
                    if (g_audio.GetEncodedVoiceData(data)) {
                        c.SendVoiceData(s.voiceServerId, s.voiceChannelId, data);
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }).detach();

        client.OnChannelListNeeded = [dispatcher]()
        {
            dispatcher.TryEnqueue([]() {
                auto& s = GetAppState();
                if (s.currentServerId != -1)
                    GetClient().RequestChannelList(s.currentServerId);
            });
        };

        client.OnMediaUploadComplete = [dispatcher](std::string url)
        {
            dispatcher.TryEnqueue([=]() {
                auto& s = GetAppState();
                if (s.currentServerId != -1 && s.currentChannelId != -1)
                    GetClient().SendServerMessage(s.currentServerId, s.currentChannelId, url);
                else if (!s.currentDM.empty() && s.currentDM != "Friends")
                    GetClient().SendPrivateMessage(s.currentDM, url);
            });
        };

        client.OnMediaReceived = [dispatcher](std::string fname, std::vector<uint8_t> data)
        {
            // Save to cache
            std::string path = GetCachePath(fname);
            std::ofstream f(path, std::ios::binary);
            if (f) f.write((const char*)data.data(), data.size());

            dispatcher.TryEnqueue([=]() {
                GetAppState().FireChanged();
            });
        };

        client.OnDisconnected = [dispatcher]()
        {
            dispatcher.TryEnqueue([]() {
                auto& s = GetAppState();
                s.isLoggedIn = false;
                s.FireChanged();
            });
        };
    }
}
