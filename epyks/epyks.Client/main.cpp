#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "Protocol/Packet.h"
#include <atomic>
#include <d3d11.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <tchar.h>
#include <thread>
#include <vector>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <sstream>
#include "AudioClient.h"
#pragma comment(lib, "ws2_32.lib")



static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace fs = std::filesystem;


std::string GetConfigPath() {
    char path[MAX_PATH];
    GetEnvironmentVariableA("APPDATA", path, MAX_PATH);
    fs::create_directories(std::string(path) + "\\Epyks");
    return std::string(path) + "\\Epyks\\config.ini";
}

void SaveCredentials(const std::string& username, const std::string& token) {
    std::ofstream file(GetConfigPath());
    if (file) {
        file << username << "\n" << token << "\n";
    }
}

bool LoadCredentials(std::string& username, std::string& token) {
    std::ifstream file(GetConfigPath());
    if (file && std::getline(file, username) && std::getline(file, token)) {
        return !username.empty() && !token.empty();
    }
    return false;
}

void ClearCredentials() {
    fs::remove(GetConfigPath());
}

struct Friend {
    std::string username;
    bool online = false;
    bool hasUnread = false;
};

struct DMChat {
    std::string username;
    std::vector<std::string> messages;
};

struct Channel {
    int id;
    std::string name;
    int type;
};

struct ServerChat {
    std::string serverName;
    std::vector<Channel> channels;
    std::map<int, std::vector<std::string>> channelMessages;
    int ID;
};

class ChatClient {
public:
    SOCKET sock = INVALID_SOCKET;
    std::atomic<bool> connected{ false };
    std::thread recvThread;
    std::vector<std::string> messages;
    std::mutex msgMutex;
    std::string username;
    std::vector<Friend> friends;
    std::vector<std::string> friendRequests;
    std::map<std::string, DMChat> dmChats;
    std::map<int, ServerChat> servers;
    std::vector<std::pair<int, std::string>> availableServers;
    std::mutex friendsMutex;

    SOCKET udpSock = INVALID_SOCKET;
    std::thread udpThread;
    std::string serverIp;
    int serverPort;
    sockaddr_in serverUdpAddr = {};
    
    AudioClient audio;
    std::atomic<bool> inVoice{false};
    int currentVoiceServerId = -1;
    int currentVoiceChannelId = -1;


    std::atomic<bool> loginSuccess{ false };
    std::atomic<bool> loginFailed{ false };
    std::string loginErrorMsg;
    std::string sessionToken;

    void SendUnfriend(const std::string& target) {
        if (!connected) return;
        epyks::Unfriend uf;
        uf.target_username = target;
        auto ufBytes = uf.Serialize();
        epyks::Packet packet;
        packet.type = epyks::PacketType::UNFRIEND;
        packet.data = std::string(ufBytes.begin(), ufBytes.end());
        auto data = packet.Serialize();
        uint32_t len = (uint32_t)data.size();
        send(sock, (char*)&len, 4, 0);
        send(sock, (char*)data.data(), len, 0);
    }

    bool Connect(const char* ip, int port) {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip, &addr.sin_addr);
        if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) return false;
        connected = true;
        
        serverIp = ip;
        serverPort = port;
        serverUdpAddr = addr;
        
        udpSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (udpSock != INVALID_SOCKET) {
            u_long mode = 1;
            ioctlsocket(udpSock, FIONBIO, &mode); // Non-blocking UDP socket
            udpThread = std::thread(&ChatClient::UdpLoop, this);
        }

        audio.Initialize();

        recvThread = std::thread(&ChatClient::ReceiveLoop, this);
        return true;
    }

    void ReceiveLoop() {
        while (connected) {
            uint32_t len = 0;
            if (recv(sock, (char*)&len, 4, MSG_WAITALL) != 4) break;
            if (len > 100000) break;
            std::vector<uint8_t> buffer(len);
            int received = 0;
            while (received < (int)len) {
                int r = recv(sock, (char*)buffer.data() + received, len - received, 0);
                if (r <= 0) { connected = false; return; }
                received += r;
            }

            epyks::Packet packet;
            if (!packet.Deserialize(buffer)) continue;

            std::lock_guard<std::mutex> lock(msgMutex);

            if (packet.type == epyks::PacketType::AUTH_PROMPT) {
                messages.push_back("System: " + packet.data);
            }
            else if (packet.type == epyks::PacketType::JOIN_LEAVE) {
                messages.push_back("*** " + packet.data + " ***");
            }
            else if (packet.type == epyks::PacketType::CHAT_MESSAGE) {
                messages.push_back(packet.data);
            }
            else if (packet.type == epyks::PacketType::FRIEND_REQUEST) {
                friendRequests.push_back(packet.data);
            }
            else if (packet.type == epyks::PacketType::FRIEND_RESPONSE) {
                messages.push_back("System: " + packet.data);
                RequestFriendList();
            }
            else if (packet.type == epyks::PacketType::FRIEND_LIST) {
                epyks::FriendList list;
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (list.Deserialize(bytes)) {
                    friends.clear();
                    for (auto& name : list.usernames) {
                        friends.push_back({ name, false, false });
                    }
                }
            }
            else if (packet.type == epyks::PacketType::PRIVATE_MESSAGE) {
                std::string data = packet.data;
                if (data.find("[DM from ") == 0) {
                    size_t start = 9;
                    size_t end = data.find("]:");
                    if (end != std::string::npos) {
                        std::string from = data.substr(start, end - start);
                        std::string msg = data.substr(end + 2);
                        dmChats[from].messages.push_back(from + ": " + msg);
                        dmChats[from].username = from;

                        for (auto& f : friends) {
                            if (f.username == from) {
                                f.hasUnread = true;
                                break;
                            }
                        }
                    }
                }
                else if (data.find("[DM to ") == 0) {
                    size_t start = 7;
                    size_t end = data.find("]:");
                    if (end != std::string::npos) {
                        std::string to = data.substr(start, end - start);
                        std::string msg = data.substr(end + 2);
                        dmChats[to].messages.push_back("You: " + msg);
                        dmChats[to].username = to;
                    }
                }
                else {
                    messages.push_back(data);
                }
            }
            else if (packet.type == epyks::PacketType::LOGIN_RESPONSE ||
                packet.type == epyks::PacketType::TOKEN_LOGIN_RESPONSE) {
                epyks::LoginResponse resp;
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (resp.Deserialize(bytes)) {
                    if (resp.success) {
                        loginSuccess = true;
                        sessionToken = resp.session_token;
                    }
                    else {
                        loginFailed = true;
                        loginErrorMsg = resp.error;
                    }
                }
            }

            else if (packet.type == epyks::PacketType::REGISTER_RESPONSE) {
                epyks::RegisterResponse resp;
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (resp.Deserialize(bytes)) {
                    if (resp.success) {

                        loginSuccess = true;

                        loginErrorMsg = "REGISTER_SUCCESS";
                    }
                    else {
                        loginFailed = true;
                        loginErrorMsg = resp.error;
                    }
                }
            }
            else if (packet.type == epyks::PacketType::SERVER_MESSAGE) {
                epyks::ServerMessage msg;
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (msg.Deserialize(bytes)) {
                    servers[msg.server_id].channelMessages[msg.channel_id].push_back(msg.content);
                }
            }
            else if (packet.type == epyks::PacketType::CREATE_SERVER) {
                messages.push_back("*** " + packet.data + " ***");
            }
            else if (packet.type == epyks::PacketType::LIST_SERVERS) {
                availableServers.clear();
                std::string data = packet.data;
                std::stringstream ss(data);
                std::string token;
                while (std::getline(ss, token, ',')) {
                    if (token.empty()) continue;
                    size_t colon = token.find(':');
                    if (colon != std::string::npos) {
                        int id = std::stoi(token.substr(0, colon));
                        std::string name = token.substr(colon + 1);
                        availableServers.push_back({ id, name });
                        if (servers.count(id) && servers[id].serverName.empty()) {
                            servers[id].serverName = name;
                            servers[id].ID = id;
                        }
                    }
                }
            }
            else if (packet.type == epyks::PacketType::JOIN_SERVER) {
                epyks::JoinServer resp;
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (resp.Deserialize(bytes)) {
                    for (auto& s : availableServers) {
                        if (s.first == resp.server_id) {
                            servers[resp.server_id].serverName = s.second;
                            servers[resp.server_id].ID = resp.server_id;
                            break;
                        }
                    }
                    if (servers[resp.server_id].serverName.empty()) {
                        SendListServers();
                    }
                    // Request channel list when joining a server
                    epyks::ChannelList req;
                    req.server_id = resp.server_id;
                    auto reqBytes = req.Serialize();
                    epyks::Packet reqPkt;
                    reqPkt.type = epyks::PacketType::CHANNEL_LIST;
                    reqPkt.data = std::string(reqBytes.begin(), reqBytes.end());
                    auto data = reqPkt.Serialize();
                    uint32_t len = (uint32_t)data.size();
                    send(sock, (char*)&len, 4, 0);
                    send(sock, (char*)data.data(), len, 0);
                }
            }
            else if (packet.type == epyks::PacketType::MY_SERVERS) {
                std::string data = packet.data;
                std::stringstream ss(data);
                std::string token;
                while (std::getline(ss, token, ',')) {
                    if (token.empty()) continue;
                    size_t colon = token.find(':');
                    if (colon != std::string::npos) {
                        int id = std::stoi(token.substr(0, colon));
                        std::string name = token.substr(colon + 1);
                        servers[id].serverName = name;
                        servers[id].ID = id;
                        
                        epyks::ChannelList req;
                        req.server_id = id;
                        auto reqBytes = req.Serialize();
                        epyks::Packet reqPkt;
                        reqPkt.type = epyks::PacketType::CHANNEL_LIST;
                        reqPkt.data = std::string(reqBytes.begin(), reqBytes.end());
                        auto outData = reqPkt.Serialize();
                        uint32_t len = (uint32_t)outData.size();
                        send(sock, (char*)&len, 4, 0);
                        send(sock, (char*)outData.data(), len, 0);
                    }
                }
            }
            else if (packet.type == epyks::PacketType::CHANNEL_LIST) {
                epyks::ChannelList list;
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (list.Deserialize(bytes)) {
                    servers[list.server_id].channels.clear();
                    std::stringstream ss(list.data);
                    std::string token;
                    while (std::getline(ss, token, ',')) {
                        if (token.empty()) continue;
                        size_t colon1 = token.find(':');
                        size_t colon2 = token.rfind(':');
                        if (colon1 != std::string::npos && colon2 != std::string::npos && colon1 != colon2) {
                            Channel c;
                            c.id = std::stoi(token.substr(0, colon1));
                            c.name = token.substr(colon1 + 1, colon2 - colon1 - 1);
                            c.type = std::stoi(token.substr(colon2 + 1));
                            servers[list.server_id].channels.push_back(c);
                        }
                    }
                }
            }
            else if (packet.type == epyks::PacketType::UNFRIEND) {
                std::string who = packet.data;
                dmChats.erase(who);
                RequestFriendList();
            }


        }
        connected = false;
    }

    void Send(const std::string& text) {
        if (!connected || text.empty()) return;
        epyks::Packet packet;
        packet.type = epyks::PacketType::CHAT_MESSAGE;
        packet.data = text;
        packet.timestamp = GetTickCount64();
        auto data = packet.Serialize();
        uint32_t len = (uint32_t)data.size();
        send(sock, (char*)&len, 4, 0);
        send(sock, (char*)data.data(), len, 0);
    }

    void SendDM(const std::string& to, const std::string& text) {
        if (!connected || text.empty()) return;
        epyks::PrivateMessage pm;
        pm.target_username = to;
        pm.content = text;
        auto pmBytes = pm.Serialize();

        epyks::Packet packet;
        packet.type = epyks::PacketType::PRIVATE_MESSAGE;
        packet.data = std::string(pmBytes.begin(), pmBytes.end());
        packet.timestamp = GetTickCount64();

        auto data = packet.Serialize();
        uint32_t len = (uint32_t)data.size();
        send(sock, (char*)&len, 4, 0);
        send(sock, (char*)data.data(), len, 0);
    }

    void RequestMyServers() {
        if (!connected) return;
        epyks::Packet packet;
        packet.type = epyks::PacketType::MY_SERVERS;
        auto data = packet.Serialize();
        uint32_t len = (uint32_t)data.size();
        send(sock, (char*)&len, 4, 0);
        send(sock, (char*)data.data(), len, 0);
    }

    //friends
    void SendFriendRequest(const std::string& target) {
        if (!connected) return;
        epyks::FriendRequest req;
        req.target_username = target;
        auto reqBytes = req.Serialize();

        epyks::Packet packet;
        packet.type = epyks::PacketType::FRIEND_REQUEST;
        packet.data = std::string(reqBytes.begin(), reqBytes.end());

        auto data = packet.Serialize();
        uint32_t len = (uint32_t)data.size();
        send(sock, (char*)&len, 4, 0);
        send(sock, (char*)data.data(), len, 0);
    }

    void RespondFriendRequest(const std::string& target, bool accept) {
        if (!connected) return;
        epyks::FriendResponse resp;
        resp.target_username = target;
        resp.accepted = accept;
        auto respBytes = resp.Serialize();

        epyks::Packet packet;
        packet.type = epyks::PacketType::FRIEND_RESPONSE;
        packet.data = std::string(respBytes.begin(), respBytes.end());

        auto data = packet.Serialize();
        uint32_t len = (uint32_t)data.size();
        send(sock, (char*)&len, 4, 0);
        send(sock, (char*)data.data(), len, 0);

        auto it = std::find(friendRequests.begin(), friendRequests.end(),
            target + " wants to add you as friend");
        if (it != friendRequests.end()) friendRequests.erase(it);
    }
    //private
    void RequestFriendList() {
        if (!connected) return;
        epyks::Packet packet;
        packet.type = epyks::PacketType::FRIEND_LIST;
        auto data = packet.Serialize();
        uint32_t len = (uint32_t)data.size();
        send(sock, (char*)&len, 4, 0);
        send(sock, (char*)data.data(), len, 0);
    }

    void SendListServers() {
        if (!connected) return;
        epyks::Packet packet;
        packet.type = epyks::PacketType::LIST_SERVERS;
        auto data = packet.Serialize();
        uint32_t len = (uint32_t)data.size();
        send(sock, (char*)&len, 4, 0);
        send(sock, (char*)data.data(), len, 0);
    }

    void GetMessages(std::vector<std::string>& out) {
        std::lock_guard<std::mutex> lock(msgMutex);
        out = messages;
    }

    //servers
    void SendCreateServer(const std::string& name) {
        if (!connected) return;
        epyks::CreateServer cg;
        cg.server_name = name;
        cg.password = "";
        auto pmBytes = cg.Serialize();
        epyks::Packet packet;
        packet.type = epyks::PacketType::CREATE_SERVER;
        packet.data = std::string(pmBytes.begin(), pmBytes.end());
        packet.timestamp = GetTickCount64();
        auto data = packet.Serialize();
        uint32_t len = (uint32_t)data.size();
        send(sock, (char*)&len, 4, 0);
        send(sock, (char*)data.data(), len, 0);
    }

    void SendJoinServer(int id) {
        if (!connected) return;
        epyks::JoinServer jg;
        jg.server_id = id;
        jg.password = "";
        auto pmBytes = jg.Serialize();
        epyks::Packet packet;
        packet.type = epyks::PacketType::JOIN_SERVER;
        packet.data = std::string(pmBytes.begin(), pmBytes.end());
        packet.timestamp = GetTickCount64();
        auto data = packet.Serialize();
        uint32_t len = (uint32_t)data.size();
        send(sock, (char*)&len, 4, 0);
        send(sock, (char*)data.data(), len, 0);
    }

    void SendLeaveServer(int id) {
        if (!connected) return;
        epyks::LeaveServer lg;
        lg.server_id = id;
        auto pmBytes = lg.Serialize();
        epyks::Packet packet;
        packet.type = epyks::PacketType::LEAVE_SERVER;
        packet.data = std::string(pmBytes.begin(), pmBytes.end());
        packet.timestamp = GetTickCount64();
        auto data = packet.Serialize();
        uint32_t len = (uint32_t)data.size();
        send(sock, (char*)&len, 4, 0);
        send(sock, (char*)data.data(), len, 0);
    }

    void SendServerMessage(int serverId, int channelId, const std::string& message) {
        if (!connected) return;
        epyks::ServerMessage gm;
        gm.server_id = serverId;
        gm.channel_id = channelId;
        gm.content = message;
        auto pmBytes = gm.Serialize();
        epyks::Packet packet;
        packet.type = epyks::PacketType::SERVER_MESSAGE;
        packet.data = std::string(pmBytes.begin(), pmBytes.end());
        packet.timestamp = GetTickCount64();
        auto data = packet.Serialize();
        uint32_t len = (uint32_t)data.size();
        send(sock, (char*)&len, 4, 0);
        send(sock, (char*)data.data(), len, 0);
    }

    void SendCreateChannel(int serverId, const std::string& name, int type) {
        if (!connected) return;
        epyks::CreateChannel cc;
        cc.server_id = serverId;
        cc.channel_name = name;
        cc.type = type;
        auto pmBytes = cc.Serialize();
        epyks::Packet packet;
        packet.type = epyks::PacketType::CREATE_CHANNEL;
        packet.data = std::string(pmBytes.begin(), pmBytes.end());
        auto data = packet.Serialize();
        uint32_t len = (uint32_t)data.size();
        send(sock, (char*)&len, 4, 0);
        send(sock, (char*)data.data(), len, 0);
    }

    void SendDeleteChannel(int serverId, int channelId) {
        if (!connected) return;
        epyks::DeleteChannel dc;
        dc.server_id = serverId;
        dc.channel_id = channelId;
        auto pmBytes = dc.Serialize();
        epyks::Packet packet;
        packet.type = epyks::PacketType::DELETE_CHANNEL;
        packet.data = std::string(pmBytes.begin(), pmBytes.end());
        auto data = packet.Serialize();
        uint32_t len = (uint32_t)data.size();
        send(sock, (char*)&len, 4, 0);
        send(sock, (char*)data.data(), len, 0);
    }

    void SendKickUser(int serverId, const std::string& username) {
        if (!connected) return;
        epyks::KickUser ku;
        ku.server_id = serverId;
        ku.target_username = username;
        auto pmBytes = ku.Serialize();
        epyks::Packet packet;
        packet.type = epyks::PacketType::KICK_USER;
        packet.data = std::string(pmBytes.begin(), pmBytes.end());
        auto data = packet.Serialize();
        uint32_t len = (uint32_t)data.size();
        send(sock, (char*)&len, 4, 0);
        send(sock, (char*)data.data(), len, 0);
    }

    void SendMuteUser(int serverId, const std::string& username, bool mute) {
        if (!connected) return;
        epyks::MuteUser mu;
        mu.server_id = serverId;
        mu.target_username = username;
        mu.is_muted = mute;
        auto pmBytes = mu.Serialize();
        epyks::Packet packet;
        packet.type = epyks::PacketType::MUTE_USER;
        packet.data = std::string(pmBytes.begin(), pmBytes.end());
        auto data = packet.Serialize();
        uint32_t len = (uint32_t)data.size();
        send(sock, (char*)&len, 4, 0);
        send(sock, (char*)data.data(), len, 0);
    }

    void SendJoinVoice(int serverId, int channelId) {
        if (!connected) return;
        epyks::JoinVoice jv;
        jv.server_id = serverId;
        jv.channel_id = channelId;
        auto bytes = jv.Serialize();
        epyks::Packet packet;
        packet.type = epyks::PacketType::JOIN_VOICE;
        packet.data = std::string(bytes.begin(), bytes.end());
        auto data = packet.Serialize();
        uint32_t len = (uint32_t)data.size();
        send(sock, (char*)&len, 4, 0);
        send(sock, (char*)data.data(), len, 0);

        currentVoiceServerId = serverId;
        currentVoiceChannelId = channelId;
        inVoice = true;
        audio.StartVoice();
        
        // Send initial UDP packet to authenticate our endpoint
        SendVoiceData({});
    }

    void SendLeaveVoice() {
        if (!connected) return;
        epyks::LeaveVoice lv;
        lv.server_id = currentVoiceServerId;
        lv.channel_id = currentVoiceChannelId;
        auto bytes = lv.Serialize();
        epyks::Packet packet;
        packet.type = epyks::PacketType::LEAVE_VOICE;
        packet.data = std::string(bytes.begin(), bytes.end());
        auto data = packet.Serialize();
        uint32_t len = (uint32_t)data.size();
        send(sock, (char*)&len, 4, 0);
        send(sock, (char*)data.data(), len, 0);

        inVoice = false;
        audio.StopVoice();
        currentVoiceServerId = -1;
        currentVoiceChannelId = -1;
    }

    void SendVoiceData(const std::vector<uint8_t>& pcm) {
        if (udpSock == INVALID_SOCKET) return;
        epyks::VoiceData vd;
        vd.username = username;
        vd.server_id = currentVoiceServerId;
        vd.channel_id = currentVoiceChannelId;
        vd.audio_data = pcm;
        auto bytes = vd.Serialize();
        sendto(udpSock, (char*)bytes.data(), bytes.size(), 0, (sockaddr*)&serverUdpAddr, sizeof(serverUdpAddr));
    }

    void UdpLoop() {
        char buffer[4096];
        while (connected) {
            if (inVoice) {
                // Send any queued outgoing voice data
                std::vector<uint8_t> outData;
                while (audio.GetEncodedVoiceData(outData)) {
                    SendVoiceData(outData);
                }
            }

            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(udpSock, &readfds);
            timeval tv = {0, 10000}; // 10ms timeout
            int ret = select(0, &readfds, NULL, NULL, &tv);
            if (ret > 0) {
                sockaddr_in senderAddr;
                int senderLen = sizeof(senderAddr);
                int bytes = recvfrom(udpSock, buffer, sizeof(buffer), 0, (sockaddr*)&senderAddr, &senderLen);
                if (bytes > 0 && inVoice) {
                    std::vector<uint8_t> data(buffer, buffer + bytes);
                    epyks::VoiceData pkt;
                    if (pkt.Deserialize(data)) {
                        audio.PushVoiceData(pkt.audio_data);
                    }
                }
            }
        }
    }


    //conection
    bool IsConnected() { return connected; }

    void Disconnect() {
        connected = false;
        if (sock != INVALID_SOCKET) {
            closesocket(sock);
            sock = INVALID_SOCKET;
        }
        if (udpSock != INVALID_SOCKET) {
            closesocket(udpSock);
            udpSock = INVALID_SOCKET;
        }
        WSACleanup();
        if (recvThread.joinable()) recvThread.join();
        if (udpThread.joinable()) udpThread.join();
        audio.Shutdown();
    }
};

struct AppConfig {
    ImVec4 bgColor = ImVec4(0.08f, 0.06f, 0.12f, 1.0f);
    ImVec4 chatBgColor = ImVec4(0.12f, 0.10f, 0.18f, 1.0f);
    ImVec4 textColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    ImVec4 ownMessageColor = ImVec4(0.75f, 0.50f, 1.0f, 1.0f);
    ImVec4 otherMessageColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    ImVec4 systemColor = ImVec4(1.0f, 0.8f, 0.4f, 1.0f);
    ImVec4 joinLeaveColor = ImVec4(0.5f, 1.0f, 0.5f, 1.0f);
    float fontSize = 16.0f;

    void Apply() {
        ImGuiStyle& style = ImGui::GetStyle();

        // rounding
        style.WindowRounding = 8.0f;
        style.ChildRounding = 6.0f;
        style.FrameRounding = 5.0f;
        style.PopupRounding = 6.0f;
        style.ScrollbarRounding = 4.0f;
        style.GrabRounding = 4.0f;
        style.TabRounding = 4.0f;

        // spacing
        style.WindowPadding = ImVec2(12, 12);
        style.FramePadding = ImVec2(8, 4);
        style.ItemSpacing = ImVec2(8, 6);
        style.ScrollbarSize = 10.0f;

        // colors
        style.Colors[ImGuiCol_WindowBg] = bgColor;
        style.Colors[ImGuiCol_ChildBg] = chatBgColor;
        style.Colors[ImGuiCol_Text] = textColor;
        style.Colors[ImGuiCol_Button] = ImVec4(0.45f, 0.15f, 0.75f, 1.0f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.55f, 0.25f, 0.85f, 1.0f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.35f, 0.10f, 0.65f, 1.0f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.25f, 1.0f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.22f, 0.32f, 1.0f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.26f, 0.38f, 1.0f);
        style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.08f, 0.15f, 1.0f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.45f, 0.15f, 0.75f, 0.5f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.45f, 0.15f, 0.75f, 0.7f);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.45f, 0.15f, 0.75f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.08f, 0.15f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.45f, 0.15f, 0.75f, 0.6f);
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.55f, 0.25f, 0.85f, 0.8f);
        style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.55f, 0.25f, 0.85f, 1.0f);
        style.Colors[ImGuiCol_Separator] = ImVec4(0.45f, 0.15f, 0.75f, 0.4f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.08f, 0.15f, 1.0f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.20f, 0.10f, 0.35f, 1.0f);
        style.Colors[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.10f, 0.18f, 1.0f);
    }
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASSEX wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, _T("Epyks"), nullptr };
    RegisterClassEx(&wc);
    HWND hwnd = CreateWindow(wc.lpszClassName, _T("Epyks Chat"), WS_OVERLAPPEDWINDOW, 100, 100, 1000, 700, nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();

    AppConfig config;
    config.Apply();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    ChatClient client;

    char groupInputBuf[256] = {};
    bool showLogin = true;
    bool showRegister = false;
    bool isConnecting = false;
    bool triedAutoLogin = false;

    char serverIP[64] = "127.0.0.1";
    char usernameBuf[64] = "";
    char passwordBuf[64] = "";
    char regUser[64] = "";
    char regPass[64] = "";
    char regConfirm[64] = "";
    bool rememberMe = true;



    char inputBuf[256] = "";
    char addFriendBuf[64] = "";
    char createServerBuf[64] = "";
    char createChannelBuf[64] = "";
    int createChannelType = 0; // 0 for Text, 1 for Voice
    char kickMuteBuf[64] = "";
    char dmInputBuf[256] = "";
    std::vector<std::string> displayMessages;
    std::string currentDM;
    int currentServerId = -1;
    int currentChannelId = -1;
    bool showSettings = false;
    bool showAddFriend = false;
    bool showFriendRequests = false;
    bool showCreateServer = false;
    bool showBrowseServers = false;
    bool showCreateChannel = false;
    bool showKickMute = false;

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;


        if (client.loginSuccess) {
            client.loginSuccess = false;

            if (client.loginErrorMsg == "REGISTER_SUCCESS") {
                showRegister = false;
                isConnecting = false;
                client.loginErrorMsg = "Registration successful! Please log in.";
                strcpy_s(usernameBuf, regUser);
                client.Disconnect();
            }
            else {

                showLogin = false;
                showRegister = false;
                isConnecting = false;
                client.username = usernameBuf;
                if (rememberMe && !client.sessionToken.empty()) {
                    SaveCredentials(client.username, client.sessionToken);
                }
                client.RequestFriendList();
                memset(passwordBuf, 0, sizeof(passwordBuf));
            }
            memset(regPass, 0, sizeof(regPass));
            memset(regConfirm, 0, sizeof(regConfirm));
            memset(regUser, 0, sizeof(regUser));
        }

        if (client.loginFailed) {
            client.loginFailed = false;
            isConnecting = false;
            client.Disconnect();
        }


        if (showLogin && !triedAutoLogin && !isConnecting) {
            triedAutoLogin = true;
            std::string savedUser, savedToken;
            if (LoadCredentials(savedUser, savedToken)) {
                strcpy_s(usernameBuf, savedUser.c_str());
                isConnecting = true;
                if (client.Connect(serverIP, 9001)) {
                    epyks::TokenLoginRequest req;
                    req.username = savedUser;
                    req.token = savedToken;
                    auto bytes = req.Serialize();
                    epyks::Packet pkt;
                    pkt.type = epyks::PacketType::TOKEN_LOGIN;
                    pkt.data = std::string(bytes.begin(), bytes.end());
                    auto data = pkt.Serialize();
                    uint32_t len = (uint32_t)data.size();
                    send(client.sock, (char*)&len, 4, 0);
                    send(client.sock, (char*)data.data(), len, 0);
                }
                else {
                    isConnecting = false;
                    ClearCredentials();
                }
            }
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImVec2 viewportSize = ImGui::GetMainViewport()->Size;

        if (showLogin || showRegister) {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(viewportSize);
            ImGui::Begin("Login", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

            float centerX = viewportSize.x * 0.5f;
            float centerY = viewportSize.y * 0.5f;

            if (showRegister) {

                ImVec2 windowPos(centerX - 150, centerY - 180);
                ImGui::SetCursorPos(windowPos);
                ImGui::BeginChild("RegisterBox", ImVec2(300, 360), true);

                ImGui::SetCursorPosX(80);
                ImGui::Text("Create Account");
                ImGui::Separator();

                if (!client.loginErrorMsg.empty()) {
                    ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s", client.loginErrorMsg.c_str());
                }

                ImGui::Text("Server IP:");
                ImGui::InputText("##ip", serverIP, 64);

                ImGui::Text("Username (min 3 chars):");
                ImGui::InputText("##reguser", regUser, 64);

                ImGui::Text("Password (min 4 chars):");
                ImGui::InputText("##regpass", regPass, 64, ImGuiInputTextFlags_Password);

                ImGui::Text("Confirm Password:");
                ImGui::InputText("##regconfirm", regConfirm, 64, ImGuiInputTextFlags_Password);

                ImGui::Checkbox("Remember me", &rememberMe);

                if (isConnecting) {
                    ImGui::Text("Creating account...");
                }
                else {
                    if (ImGui::Button("Create Account", ImVec2(140, 30))) {
                        if (strlen(regUser) < 3) {
                            client.loginErrorMsg = "Username too short (min 3)";
                        }
                        else if (strlen(regPass) < 4) {
                            client.loginErrorMsg = "Password too short (min 4)";
                        }
                        else if (strcmp(regPass, regConfirm) != 0) {
                            client.loginErrorMsg = "Passwords don't match";
                        }
                        else {
                            isConnecting = true;
                            client.loginErrorMsg = "";
                            if (client.Connect(serverIP, 9001)) {
                                epyks::RegisterRequest req;
                                req.username = regUser;
                                req.password = regPass;
                                auto bytes = req.Serialize();
                                epyks::Packet pkt;
                                pkt.type = epyks::PacketType::REGISTER;
                                pkt.data = std::string(bytes.begin(), bytes.end());
                                auto data = pkt.Serialize();
                                uint32_t len = (uint32_t)data.size();
                                send(client.sock, (char*)&len, 4, 0);
                                send(client.sock, (char*)data.data(), len, 0);
                            }
                            else {
                                isConnecting = false;
                                client.loginErrorMsg = "Failed to connect";
                            }
                        }
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Back", ImVec2(100, 30))) {
                        showRegister = false;
                        client.loginErrorMsg = "";
                    }
                }
                ImGui::EndChild();
            }
            else {

                ImVec2 windowPos(centerX - 150, centerY - 150);
                ImGui::SetCursorPos(windowPos);
                ImGui::BeginChild("LoginBox", ImVec2(300, 300), true);

                ImGui::SetCursorPosX(100);
                ImGui::Text("Epyks Login");
                ImGui::Separator();

                if (!client.loginErrorMsg.empty()) {
                    ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s", client.loginErrorMsg.c_str());
                }

                ImGui::Text("Server IP:");
                ImGui::InputText("##ip", serverIP, 64);

                ImGui::Text("Username:");
                ImGui::InputText("##username", usernameBuf, 64);

                ImGui::Text("Password:");
                ImGui::InputText("##password", passwordBuf, 64, ImGuiInputTextFlags_Password);

                ImGui::Checkbox("Remember me", &rememberMe);

                if (isConnecting) {
                    ImGui::Text("Connecting...");
                }
                else {
                    if (ImGui::Button("Login", ImVec2(120, 30))) {
                        if (strlen(usernameBuf) == 0 || strlen(passwordBuf) == 0) {
                            client.loginErrorMsg = "Enter username and password";
                        }
                        else {
                            isConnecting = true;
                            client.loginErrorMsg = "";
                            if (client.Connect(serverIP, 9001)) {
                                epyks::LoginRequest req;
                                req.username = usernameBuf;
                                req.password = passwordBuf;
                                auto bytes = req.Serialize();
                                epyks::Packet pkt;
                                pkt.type = epyks::PacketType::LOGIN;
                                pkt.data = std::string(bytes.begin(), bytes.end());
                                auto data = pkt.Serialize();
                                uint32_t len = (uint32_t)data.size();
                                send(client.sock, (char*)&len, 4, 0);
                                send(client.sock, (char*)data.data(), len, 0);
                            }
                            else {
                                isConnecting = false;
                                client.loginErrorMsg = "Failed to connect to server";
                            }
                        }
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Register", ImVec2(120, 30))) {
                        showRegister = true;
                        client.loginErrorMsg = "";
                    }
                }
                ImGui::EndChild();
            }
            ImGui::End();
        }
        else {

            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(viewportSize);
            ImGui::Begin("Epyks Chat", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_MenuBar);

            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("Logout")) {
                        ClearCredentials();
                        client.Disconnect();
                        showLogin = true;
                        triedAutoLogin = false;
                        client.loginErrorMsg = "";
                        isConnecting = false;
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Friends")) {
                    if (ImGui::MenuItem("Add Friend...")) showAddFriend = true;
                    if (ImGui::MenuItem("Friend Requests")) showFriendRequests = true;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Settings")) {
                    if (ImGui::MenuItem("Colors...")) showSettings = true;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Servers")) {
                    if (ImGui::MenuItem("Create Server...")) showCreateServer = true;
                    if (ImGui::MenuItem("Browse Servers...")) showBrowseServers = true;
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }


            float serverSidebarWidth = 70;
            float channelSidebarWidth = 240;
            
            // Pane 1: Server Sidebar
            ImGui::BeginChild("ServerSidebar", ImVec2(serverSidebarWidth, -30), true);
            
            // Home Button
            ImGui::PushStyleColor(ImGuiCol_Button, currentServerId == -1 ? ImVec4(0.35f, 0.10f, 0.65f, 1.0f) : ImVec4(0.15f, 0.15f, 0.20f, 1.0f));
            if (ImGui::Button("HM", ImVec2(50, 50))) {
                currentServerId = -1;
                currentChannelId = -1;
            }
            ImGui::PopStyleColor();
            
            ImGui::Separator();
            
            // Server List
            for (auto& s : client.servers) {
                ImGui::PushStyleColor(ImGuiCol_Button, currentServerId == s.first ? ImVec4(0.35f, 0.10f, 0.65f, 1.0f) : ImVec4(0.15f, 0.15f, 0.20f, 1.0f));
                std::string initial = s.second.serverName.substr(0, 2);
                if (ImGui::Button(initial.c_str(), ImVec2(50, 50))) {
                    currentServerId = s.first;
                    currentChannelId = -1;
                    currentDM = "";
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", s.second.serverName.c_str());
                }
                ImGui::PopStyleColor();
            }
            ImGui::EndChild();
            ImGui::SameLine();

            // Pane 2: Channel/Friends Sidebar
            ImGui::BeginChild("ChannelSidebar", ImVec2(channelSidebarWidth, -30), true);
            if (currentServerId == -1) {
                // Friends List
                ImGui::Text("Friends");
                ImGui::Separator();
                
                std::lock_guard<std::mutex> lock(client.friendsMutex);
                for (auto& friend_ : client.friends) {
                    ImVec4 color = friend_.hasUnread ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f) : config.textColor;
                    ImGui::PushStyleColor(ImGuiCol_Text, color);
                    std::string label = friend_.username;
                    if (friend_.hasUnread) label += " *";
                    if (ImGui::Button(label.c_str(), ImVec2(-1, 30))) {
                        currentDM = friend_.username;
                        friend_.hasUnread = false;
                    }
                    if (ImGui::BeginPopupContextItem(("ctx##" + friend_.username).c_str())) {
                        if (ImGui::MenuItem("Unfriend")) {
                            client.SendUnfriend(friend_.username);
                            client.dmChats.erase(friend_.username);
                            if (currentDM == friend_.username) currentDM = "";
                            client.RequestFriendList();
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::PopStyleColor();
                }
            } else {
                // Channel List
                auto& server = client.servers[currentServerId];
                ImGui::Text("%s", server.serverName.c_str());
                ImGui::Separator();
                
                for (auto& channel : server.channels) {
                    std::string label = (channel.type == 0 ? "# " : "v ") + channel.name;
                    if (ImGui::Selectable(label.c_str(), currentChannelId == channel.id)) {
                        currentChannelId = channel.id;
                    }
                    if (ImGui::BeginPopupContextItem(("ctx_channel##" + std::to_string(channel.id)).c_str())) {
                        if (ImGui::MenuItem("Delete Channel")) {
                            client.SendDeleteChannel(currentServerId, channel.id);
                        }
                        ImGui::EndPopup();
                    }
                }
                
                ImGui::Separator();
                if (ImGui::Button("Create Channel", ImVec2(-1, 30))) showCreateChannel = true;
                if (ImGui::Button("Moderate Users", ImVec2(-1, 30))) showKickMute = true;
                if (ImGui::Button("Leave Server", ImVec2(-1, 30))) {
                    client.SendLeaveServer(currentServerId);
                    client.servers.erase(currentServerId);
                    currentServerId = -1;
                    currentChannelId = -1;
                }
            }
            ImGui::EndChild();
            ImGui::SameLine();

            // Pane 3: Chat Area
            float chatWidth = viewportSize.x - serverSidebarWidth - channelSidebarWidth - 25;
            ImGui::BeginChild("ChatArea", ImVec2(chatWidth, -30), false);
            
            if (currentServerId != -1 && currentChannelId != -1) {
                auto& server = client.servers[currentServerId];
                std::string channelName = "Unknown";
                int channelType = 0;
                for (auto& c : server.channels) {
                    if (c.id == currentChannelId) {
                        channelName = c.name;
                        channelType = c.type;
                    }
                }
                
                if (channelType == 0) { // Text Channel
                    ImGui::Text("Channel: #%s", channelName.c_str());
                    ImGui::Separator();
                    
                    float chatHeight = ImGui::GetContentRegionAvail().y - 40;
                    ImGui::BeginChild("ChannelHistory", ImVec2(0, chatHeight), true);
                    auto& messages = server.channelMessages[currentChannelId];
                    for (auto& m : messages) {
                        ImVec4 color;
                        if (m.find("***") == 0) color = config.joinLeaveColor;
                        else if (m.find("System:") == 0) color = config.systemColor;
                        else if (m.find("[" + client.username + "]:") != std::string::npos)
                            color = config.ownMessageColor;
                        else color = config.otherMessageColor;
                        ImGui::PushStyleColor(ImGuiCol_Text, color);
                        ImGui::Selectable(m.c_str(), false);
                        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1)) {
                            ImGui::SetClipboardText(m.c_str());
                        }
                        ImGui::PopStyleColor();
                    }
                    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
                    ImGui::EndChild();
                    
                    ImGui::Separator();
                    static bool refocusInput = false;
                    if (refocusInput) {
                        ImGui::SetKeyboardFocusHere();
                        refocusInput = false;
                    }
                    if (ImGui::InputText("##channelinput", inputBuf, 256, ImGuiInputTextFlags_EnterReturnsTrue)) {
                        client.SendServerMessage(currentServerId, currentChannelId, inputBuf);
                        inputBuf[0] = '\0';
                        refocusInput = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Send")) {
                        client.SendServerMessage(currentServerId, currentChannelId, inputBuf);
                        inputBuf[0] = '\0';
                        refocusInput = true;
                    }
                } else if (channelType == 1) { // Voice Channel
                    ImGui::Text("Voice Channel: v %s", channelName.c_str());
                    ImGui::Separator();
                    
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20);
                    
                    if (client.inVoice && client.currentVoiceServerId == currentServerId && client.currentVoiceChannelId == currentChannelId) {
                        ImGui::TextColored(ImVec4(0, 1, 0, 1), "Voice Connected - Transmitting Audio");
                        ImGui::Dummy(ImVec2(0, 10));
                        if (ImGui::Button("Disconnect", ImVec2(150, 40))) {
                            client.SendLeaveVoice();
                        }
                    } else {
                        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1), "Ready to connect to Voice");
                        ImGui::Dummy(ImVec2(0, 10));
                        if (ImGui::Button("Connect", ImVec2(150, 40))) {
                            if (client.inVoice) {
                                client.SendLeaveVoice(); // Leave current before joining new
                            }
                            client.SendJoinVoice(currentServerId, currentChannelId);
                        }
                    }
                }
            } else if (currentServerId == -1 && !currentDM.empty()) {
                ImGui::Text("DM with %s", currentDM.c_str());
                ImGui::Separator();
                
                float chatHeight = ImGui::GetContentRegionAvail().y - 40;
                ImGui::BeginChild("DMHistory", ImVec2(0, chatHeight), true);
                auto& dm = client.dmChats[currentDM];
                for (auto& m : dm.messages) {
                    ImVec4 color = m.find("You:") == 0 ? config.ownMessageColor : config.otherMessageColor;
                    ImGui::PushStyleColor(ImGuiCol_Text, color);
                    ImGui::TextWrapped("%s", m.c_str());
                    ImGui::PopStyleColor();
                }
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
                ImGui::EndChild();
                
                ImGui::Separator();
                static bool refocusDM = false;
                if (refocusDM) {
                    ImGui::SetKeyboardFocusHere();
                    refocusDM = false;
                }
                if (ImGui::InputText("##dminput", dmInputBuf, 256, ImGuiInputTextFlags_EnterReturnsTrue)) {
                    client.SendDM(currentDM, dmInputBuf);
                    dmInputBuf[0] = '\0';
                    refocusDM = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Send DM")) {
                    client.SendDM(currentDM, dmInputBuf);
                    dmInputBuf[0] = '\0';
                    refocusDM = true;
                }
            } else if (currentServerId == -1) {
                // Global Chat
                ImGui::Text("Global Chat");
                ImGui::Separator();
                
                client.GetMessages(displayMessages);
                float chatHeight = ImGui::GetContentRegionAvail().y - 40;
                ImGui::BeginChild("ChatHistory", ImVec2(0, chatHeight), true);
                for (auto& m : displayMessages) {
                    ImVec4 color;
                    if (m.find("***") == 0) color = config.joinLeaveColor;
                    else if (m.find("System:") == 0) color = config.systemColor;
                    else if (m.find("[" + client.username + "]:") != std::string::npos)
                        color = config.ownMessageColor;
                    else color = config.otherMessageColor;
                    ImGui::PushStyleColor(ImGuiCol_Text, color);
                    ImGui::Selectable(m.c_str(), false);
                    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1)) {
                        ImGui::SetClipboardText(m.c_str());
                    }
                    ImGui::PopStyleColor();
                }
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
                ImGui::EndChild();
                
                ImGui::Separator();
                static bool refocusInput = false;
                if (refocusInput) {
                    ImGui::SetKeyboardFocusHere();
                    refocusInput = false;
                }
                if (ImGui::InputText("##input", inputBuf, 256, ImGuiInputTextFlags_EnterReturnsTrue)) {
                    client.Send(inputBuf);
                    inputBuf[0] = '\0';
                    refocusInput = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Send")) {
                    client.Send(inputBuf);
                    inputBuf[0] = '\0';
                    refocusInput = true;
                }
            }
            ImGui::EndChild();
            
            ImGui::End();


            if (showAddFriend) ImGui::OpenPopup("Add Friend");
            ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            if (ImGui::BeginPopupModal("Add Friend", &showAddFriend, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Enter username:");
                ImGui::InputText("##friend", addFriendBuf, 64);
                if (ImGui::Button("Send Request")) {
                    client.SendFriendRequest(addFriendBuf);
                    addFriendBuf[0] = '\0';
                    showAddFriend = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    showAddFriend = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }


            if (showFriendRequests) ImGui::OpenPopup("Friend Requests");
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            if (ImGui::BeginPopupModal("Friend Requests", &showFriendRequests, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Incoming Requests:");
                ImGui::Separator();

                for (auto& req : client.friendRequests) {
                    size_t pos = req.find(" wants");
                    if (pos != std::string::npos) {
                        std::string name = req.substr(0, pos);
                        ImGui::Text("%s", name.c_str());
                        ImGui::SameLine();
                        if (ImGui::Button(("Accept##" + name).c_str())) {
                            client.RespondFriendRequest(name, true);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button(("Decline##" + name).c_str())) {
                            client.RespondFriendRequest(name, false);
                        }
                    }
                }

                if (client.friendRequests.empty()) {
                    ImGui::Text("No pending requests");
                }

                if (ImGui::Button("Close")) {
                    showFriendRequests = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            if (showCreateServer) ImGui::OpenPopup("Create Server");
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            if (ImGui::BeginPopupModal("Create Server", &showCreateServer, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Server name:");
                ImGui::InputText("##createserver", createServerBuf, 64);
                if (ImGui::Button("Create")) {
                    client.SendCreateServer(createServerBuf);
                    createServerBuf[0] = '\0';
                    showCreateServer = false;
                    ImGui::CloseCurrentPopup();

                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    showCreateServer = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            static bool sentListRequest = false;
            if (showBrowseServers) {
                if (!sentListRequest) {
                    client.SendListServers();
                    sentListRequest = true;
                }
                ImGui::OpenPopup("Browse Servers");
            }
            if (!showBrowseServers) sentListRequest = false;
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            if (ImGui::BeginPopupModal("Browse Servers", &showBrowseServers, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Available Servers:");
                ImGui::Separator();

                for (auto& s : client.availableServers) {
                    ImGui::Text("%s", s.second.c_str());
                    ImGui::SameLine();
                    if (ImGui::Button(("Join##" + std::to_string(s.first)).c_str())) {
                        client.SendJoinServer(s.first);
                        showBrowseServers = false;
                        ImGui::CloseCurrentPopup();
                    }
                }

                if (ImGui::Button("Close")) {
                    showBrowseServers = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            if (showCreateChannel) ImGui::OpenPopup("Create Channel");
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            if (ImGui::BeginPopupModal("Create Channel", &showCreateChannel, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Channel name:");
                ImGui::InputText("##createchannel", createChannelBuf, 64);
                ImGui::RadioButton("Text Channel", &createChannelType, 0); ImGui::SameLine();
                ImGui::RadioButton("Voice Channel", &createChannelType, 1);
                
                if (ImGui::Button("Create")) {
                    client.SendCreateChannel(currentServerId, createChannelBuf, createChannelType);
                    createChannelBuf[0] = '\0';
                    showCreateChannel = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    showCreateChannel = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            if (showKickMute) ImGui::OpenPopup("Moderate Users");
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            if (ImGui::BeginPopupModal("Moderate Users", &showKickMute, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Target Username:");
                ImGui::InputText("##targetuser", kickMuteBuf, 64);
                
                if (ImGui::Button("Kick")) {
                    client.SendKickUser(currentServerId, kickMuteBuf);
                    kickMuteBuf[0] = '\0';
                    showKickMute = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Mute")) {
                    client.SendMuteUser(currentServerId, kickMuteBuf, true);
                    kickMuteBuf[0] = '\0';
                    showKickMute = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Unmute")) {
                    client.SendMuteUser(currentServerId, kickMuteBuf, false);
                    kickMuteBuf[0] = '\0';
                    showKickMute = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    showKickMute = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            if (showSettings) {
                ImGui::OpenPopup("Settings");
            }
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(400, 500));
            if (ImGui::BeginPopupModal("Settings", &showSettings, ImGuiWindowFlags_NoResize)) {
                ImGui::Text("Background Colors");
                ImGui::ColorEdit4("Main BG", (float*)&config.bgColor);
                ImGui::ColorEdit4("Chat BG", (float*)&config.chatBgColor);

                ImGui::Separator();
                ImGui::Text("Text Colors");
                ImGui::ColorEdit4("Your Messages", (float*)&config.ownMessageColor);
                ImGui::ColorEdit4("Others' Messages", (float*)&config.otherMessageColor);
                ImGui::ColorEdit4("System Messages", (float*)&config.systemColor);
                ImGui::ColorEdit4("Join/Leave", (float*)&config.joinLeaveColor);

                if (ImGui::Button("Apply", ImVec2(120, 0))) config.Apply();
                ImGui::SameLine();
                if (ImGui::Button("Reset Defaults", ImVec2(120, 0))) {
                    config = AppConfig();
                    config.Apply();
                }
                ImGui::SameLine();
                if (ImGui::Button("Close", ImVec2(120, 0))) {
                    showSettings = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }

        ImGui::Render();
        const float clear_color[4] = { config.bgColor.x, config.bgColor.y, config.bgColor.z, config.bgColor.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    client.Disconnect();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);

    return 0;
}

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK) return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
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