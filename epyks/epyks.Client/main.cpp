#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "AudioClient.h"
#include "Protocol/Packet.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include <atomic>
#include <d3d11.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <tchar.h>
#include <thread>
#include <vector>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <commdlg.h>
#include <tuple>
#include <algorithm>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comdlg32.lib")

#define STB_IMAGE_IMPLEMENTATION
#include "../../deps/stb/stb_image.h"

struct TextureInfo {
    ID3D11ShaderResourceView* srv = nullptr;
    int width = 0;
    int height = 0;
};
static std::map<std::string, TextureInfo> g_textureCache;

TextureInfo LoadTextureFromFile(const char* filename, ID3D11Device* pd3dDevice) {
    TextureInfo info;
    int width, height, channels;
    unsigned char* data = stbi_load(filename, &width, &height, &channels, 4);
    if (data == NULL) return info;

    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    ID3D11Texture2D* pTexture = NULL;
    D3D11_SUBRESOURCE_DATA subResource;
    subResource.pSysMem = data;
    subResource.SysMemPitch = width * 4;
    subResource.SysMemSlicePitch = 0;
    pd3dDevice->CreateTexture2D(&desc, &subResource, &pTexture);

    if (pTexture) {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
        ZeroMemory(&srvDesc, sizeof(srvDesc));
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        pd3dDevice->CreateShaderResourceView(pTexture, &srvDesc, &info.srv);
        pTexture->Release();
        info.width = width;
        info.height = height;
    }

    stbi_image_free(data);
    return info;
}

static ID3D11Device *g_pd3dDevice = nullptr;
static ID3D11DeviceContext *g_pd3dDeviceContext = nullptr;
static IDXGISwapChain *g_pSwapChain = nullptr;
static ID3D11RenderTargetView *g_mainRenderTargetView = nullptr;

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

void SaveConfig(const std::string &username, const std::string &token,
                const std::string &inDev = "", const std::string &outDev = "") {
  std::ofstream file(GetConfigPath());
  if (file) {
    file << username << "\n"
         << token << "\n"
         << inDev << "\n"
         << outDev << "\n";
  }
}

bool LoadConfig(std::string &username, std::string &token, std::string &inDev,
                std::string &outDev) {
  std::ifstream file(GetConfigPath());
  if (file && std::getline(file, username) && std::getline(file, token)) {
    std::getline(file, inDev);
    std::getline(file, outDev);
    return !username.empty() && !token.empty();
  }
  return false;
}

void ClearCredentials() { fs::remove(GetConfigPath()); }

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
  std::string category;
};

struct Message {
    std::string sender;
    std::string content;
};

struct ServerChat {
  std::string serverName;
  std::vector<Channel> channels;
  std::map<int, std::vector<Message>> channelMessages;
  int ID;
};

class ChatClient {
public:
  SOCKET sock = INVALID_SOCKET;
  std::atomic<bool> connected{false};
  std::thread recvThread;
  std::vector<std::string> messages;
  std::mutex msgMutex;
  std::string username;
  std::vector<Friend> friends;
  std::vector<std::string> friendRequests;
  std::map<int, ServerChat> servers;
  std::map<int, std::vector<epyks::MemberInfo>> serverMembers;
  std::map<std::string, epyks::UserProfile> userProfileCache;
  std::vector<std::tuple<int, std::string, bool>> availableServers;
  std::map<std::string, DMChat> dmChats;
  std::mutex friendsMutex;
  epyks::UserProfile myProfile;

  SOCKET udpSock = INVALID_SOCKET;
  std::thread udpThread;
  std::string serverIp;
  int serverPort;
  sockaddr_in serverUdpAddr = {};

  AudioClient audio;
  std::atomic<bool> inVoice{false};
  int currentVoiceServerId = -1;
  int currentVoiceChannelId = -1;
  std::atomic<int> currentServerId{-1};
  std::atomic<int> currentChannelId{-1};
  std::string pendingProfileRequest;
  bool pendingProfileSilent = false;
  std::string selectedProfileUser;
  bool showProfileModal = false;
  std::string lastServerOpStatus = "";
  epyks::UserProfile selectedProfile;
  
  bool showVoiceCallWindow = false;
  bool isMuted = false;
  bool isDeafened = false;
  
  bool showPasswordModal = false;
  int targetServerToJoin = -1;
  char serverPassBuf[64] = {0};

  std::atomic<bool> loginSuccess{false};
  std::atomic<bool> loginFailed{false};
  std::string loginErrorMsg;
  std::string sessionToken;

  void SendUnfriend(const std::string &target) {
    if (!connected)
      return;
    epyks::Unfriend uf;
    uf.target_username = target;
    auto ufBytes = uf.Serialize();
    epyks::Packet packet;
    packet.type = epyks::PacketType::UNFRIEND;
    packet.data = std::string(ufBytes.begin(), ufBytes.end());
    auto data = packet.Serialize();
    uint32_t len = (uint32_t)data.size();
    send(sock, (char *)&len, 4, 0);
    send(sock, (char *)data.data(), len, 0);
  }

  bool Connect(const char *ip, int port) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    if (connect(sock, (sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR)
      return false;
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
      if (recv(sock, (char *)&len, 4, MSG_WAITALL) != 4)
        break;
      if (len > 100000)
        break;
      std::vector<uint8_t> buffer(len);
      int received = 0;
      while (received < (int)len) {
        int r = recv(sock, (char *)buffer.data() + received, len - received, 0);
        if (r <= 0) {
          connected = false;
          return;
        }
        received += r;
      }

      epyks::Packet packet;
      if (!packet.Deserialize(buffer))
        continue;

      std::lock_guard<std::mutex> lock(msgMutex);

      if (packet.type == epyks::PacketType::AUTH_PROMPT) {
        messages.push_back("System: " + packet.data);
      } else if (packet.type == epyks::PacketType::JOIN_LEAVE) {
        messages.push_back("*** " + packet.data + " ***");
      } else if (packet.type == epyks::PacketType::CHAT_MESSAGE) {
        messages.push_back(packet.data);
      } else if (packet.type == epyks::PacketType::FRIEND_REQUEST) {
        friendRequests.push_back(packet.data);
      } else if (packet.type == epyks::PacketType::FRIEND_RESPONSE) {
        messages.push_back("System: " + packet.data);
        RequestFriendList();
      } else if (packet.type == epyks::PacketType::FRIEND_LIST) {
        epyks::FriendList list;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (list.Deserialize(bytes)) {
          friends.clear();
          for (auto &name : list.usernames) {
            friends.push_back({name, false, false});
          }
        }
      } else if (packet.type == epyks::PacketType::PRIVATE_MESSAGE) {
        std::string data = packet.data;
        if (data.find("[DM from ") == 0) {
          size_t start = 9;
          size_t end = data.find("]:");
          if (end != std::string::npos) {
            std::string from = data.substr(start, end - start);
            std::string msg = data.substr(end + 2);
            dmChats[from].messages.push_back(from + ": " + msg);
            dmChats[from].username = from;

            for (auto &f : friends) {
              if (f.username == from) {
                f.hasUnread = true;
                break;
              }
            }
          }
        } else if (data.find("[DM to ") == 0) {
          size_t start = 7;
          size_t end = data.find("]:");
          if (end != std::string::npos) {
            std::string to = data.substr(start, end - start);
            std::string msg = data.substr(end + 2);
            dmChats[to].messages.push_back("You: " + msg);
            dmChats[to].username = to;
          }
        } else {
          messages.push_back(data);
        }
      } else if (packet.type == epyks::PacketType::LOGIN_RESPONSE ||
                 packet.type == epyks::PacketType::TOKEN_LOGIN_RESPONSE) {
        epyks::LoginResponse resp;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (resp.Deserialize(bytes)) {
          if (resp.success) {
            sessionToken = resp.session_token;
            loginSuccess = true;
            myProfile.username = username;
            myProfile.pfp_url = ""; // Will be updated by RequestProfile
            userProfileCache[username] = myProfile;
            SaveConfig(username, sessionToken, "", "");
            RequestMyServers();
            RequestFriendList();
            RequestProfile("", true); // Get our full profile silently
          } else {
            loginFailed = true;
            loginErrorMsg = resp.error;
          }
        }
      } else if (packet.type == epyks::PacketType::REGISTER_RESPONSE) {
        epyks::RegisterResponse resp;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (resp.Deserialize(bytes)) {
          if (resp.success) {
            loginSuccess = true;
            loginErrorMsg = "REGISTER_SUCCESS";
          } else {
            loginFailed = true;
            loginErrorMsg = resp.error;
          }
        }
      } else if (packet.type == epyks::PacketType::SERVER_MESSAGE) {
        epyks::ServerMessage msg;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (msg.Deserialize(bytes)) {
          Message m;
          m.sender = msg.username;
          m.content = msg.content;
          servers[msg.server_id].channelMessages[msg.channel_id].push_back(m);
          
          if (userProfileCache.find(m.sender) == userProfileCache.end()) {
              RequestProfile(m.sender, true);
          }
        }
      } else if (packet.type == epyks::PacketType::CREATE_SERVER) {
        messages.push_back("*** " + packet.data + " ***");
        RequestMyServers();
      } else if (packet.type == epyks::PacketType::RENAME_SERVER) {
        lastServerOpStatus = packet.data;
        RequestMyServers();
      } else if (packet.type == epyks::PacketType::CREATE_CHANNEL) {
        lastServerOpStatus = packet.data;
        if (currentServerId != -1) {
            epyks::ChannelList req;
            req.server_id = currentServerId;
            auto reqBytes = req.Serialize();
            epyks::Packet reqPkt;
            reqPkt.type = epyks::PacketType::CHANNEL_LIST;
            reqPkt.data = std::string(reqBytes.begin(), reqBytes.end());
            auto data = reqPkt.Serialize();
            uint32_t len = (uint32_t)data.size();
            send(sock, (char *)&len, 4, 0);
            send(sock, (char *)data.data(), len, 0);
        }
      } else if (packet.type == epyks::PacketType::LIST_SERVERS) {
        availableServers.clear();
        std::string data = packet.data;
        std::stringstream ss(data);
        std::string token;
        while (std::getline(ss, token, ',')) {
          if (token.empty())
            continue;
          std::stringstream sSS(token);
          std::string idStr, name, hasPassStr;
          if (std::getline(sSS, idStr, ':') && std::getline(sSS, name, ':') && std::getline(sSS, hasPassStr, ':')) {
              int id = std::stoi(idStr);
              bool hasPass = (hasPassStr == "1");
              availableServers.push_back({id, name, hasPass});
          }
        }
      } else if (packet.type == epyks::PacketType::JOIN_SERVER) {
        epyks::JoinServer resp;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (resp.Deserialize(bytes)) {
          for (auto &s : availableServers) {
            if (std::get<0>(s) == resp.server_id) {
              servers[resp.server_id].serverName = std::get<1>(s);
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
          send(sock, (char *)&len, 4, 0);
          send(sock, (char *)data.data(), len, 0);
        }
      } else if (packet.type == epyks::PacketType::LEAVE_SERVER) {
        try {
          int srvId = std::stoi(packet.data);
          servers.erase(srvId);
          if (this->currentServerId == srvId)
            this->currentServerId = -1;
        } catch (...) {
        }
      } else if (packet.type == epyks::PacketType::MY_SERVERS) {
        servers.clear();
        std::string data = packet.data;
        std::stringstream ss(data);
        std::string token;
        while (std::getline(ss, token, ',')) {
          if (token.empty())
            continue;
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
            auto data = reqPkt.Serialize();
            uint32_t len = (uint32_t)data.size();
            send(sock, (char *)&len, 4, 0);
            send(sock, (char *)data.data(), len, 0);
          }
        }
      } else if (packet.type == epyks::PacketType::CHANNEL_LIST) {
        epyks::ChannelList list;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (list.Deserialize(bytes)) {
          servers[list.server_id].channels.clear();
          std::stringstream ss(list.data);
          std::string token;
          while (std::getline(ss, token, ',')) {
            if (token.empty())
              continue;
            std::stringstream chSS(token);
            std::string idStr, name, typeStr, category;
            if (std::getline(chSS, idStr, ':') &&
                std::getline(chSS, name, ':') &&
                std::getline(chSS, typeStr, ':') &&
                std::getline(chSS, category, ':')) {
              Channel c;
              c.id = std::stoi(idStr);
              c.name = name;
              c.type = std::stoi(typeStr);
              c.category = category;
              servers[list.server_id].channels.push_back(c);
            }
          }
          
          // Auto-select first text channel if none selected
          if (currentServerId == list.server_id && currentChannelId == -1) {
              for (auto& ch : servers[list.server_id].channels) {
                  if (ch.type == 0) { // Text
                      currentChannelId = ch.id;
                      servers[list.server_id].channelMessages[ch.id].clear();
                      RequestServerHistory(list.server_id, ch.id);
                      break;
                  }
              }
          }
        }
      } else if (packet.type == epyks::PacketType::UNFRIEND) {
        std::string who = packet.data;
        dmChats.erase(who);
        RequestFriendList();
      } else if (packet.type == epyks::PacketType::PROFILE_DATA) {
        epyks::UserProfile profile;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (profile.Deserialize(bytes)) {
          userProfileCache[profile.username] = profile;
          if (profile.username == username) {
            myProfile = profile;
          }
          // Update local member cache for all servers for THIS user
          for (auto& pair : serverMembers) {
              for (auto& m : pair.second) {
                  if (m.username == profile.username) {
                      m.bio = profile.bio;
                      m.pfp_url = profile.pfp_url;
                  }
              }
          }
          if (profile.username == pendingProfileRequest) {
            if (!pendingProfileSilent) {
                selectedProfile = profile;
                showProfileModal = true;
            }
            pendingProfileRequest = "";
            pendingProfileSilent = false;
          }
        }
      } else if (packet.type == epyks::PacketType::MEMBER_LIST_RESPONSE) {
        epyks::MemberListResponse res;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (res.Deserialize(bytes)) {
          serverMembers[res.server_id] = res.members;
          for (auto& m : res.members) {
              userProfileCache[m.username].username = m.username;
              userProfileCache[m.username].pfp_url = m.pfp_url;
              userProfileCache[m.username].bio = m.bio;
          }
        }
      }
    }
    connected = false;
  }

  void Send(const std::string &text) {
    if (!connected || text.empty())
      return;
    epyks::Packet packet;
    packet.type = epyks::PacketType::CHAT_MESSAGE;
    packet.data = text;
    packet.timestamp = GetTickCount64();
    auto data = packet.Serialize();
    uint32_t len = (uint32_t)data.size();
    send(sock, (char *)&len, 4, 0);
    send(sock, (char *)data.data(), len, 0);
  }

  void SendDM(const std::string &to, const std::string &text) {
    if (!connected || text.empty())
      return;
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
    send(sock, (char *)&len, 4, 0);
    send(sock, (char *)data.data(), len, 0);
  }

  void RequestMyServers() {
    if (!connected)
      return;
    epyks::Packet packet;
    packet.type = epyks::PacketType::MY_SERVERS;
    auto data = packet.Serialize();
    uint32_t len = (uint32_t)data.size();
    send(sock, (char *)&len, 4, 0);
    send(sock, (char *)data.data(), len, 0);
  }

  void Login(const std::string &u, const std::string &p) {
    if (!connected)
      return;
    username = u;
    epyks::LoginRequest req;
    req.username = u;
    req.password = p;
    auto bytes = req.Serialize();
    epyks::Packet pkt;
    pkt.type = epyks::PacketType::LOGIN;
    pkt.data = std::string(bytes.begin(), bytes.end());
    auto data = pkt.Serialize();
    uint32_t len = (uint32_t)data.size();
    send(sock, (char *)&len, 4, 0);
    send(sock, (char *)data.data(), len, 0);
  }

  void Register(const std::string &u, const std::string &p) {
    if (!connected)
      return;
    epyks::RegisterRequest req;
    req.username = u;
    req.password = p;
    auto bytes = req.Serialize();
    epyks::Packet pkt;
    pkt.type = epyks::PacketType::REGISTER;
    pkt.data = std::string(bytes.begin(), bytes.end());
    auto data = pkt.Serialize();
    uint32_t len = (uint32_t)data.size();
    send(sock, (char *)&len, 4, 0);
    send(sock, (char *)data.data(), len, 0);
  }

  void RequestMemberList(int serverId) {
    if (!connected)
      return;
    epyks::MemberListRequest req;
    req.server_id = serverId;
    auto bytes = req.Serialize();
    epyks::Packet pkt;
    pkt.type = epyks::PacketType::MEMBER_LIST_REQUEST;
    pkt.data = std::string(bytes.begin(), bytes.end());
    auto data = pkt.Serialize();
    uint32_t len = (uint32_t)data.size();
    send(sock, (char *)&len, 4, 0);
    send(sock, (char *)data.data(), len, 0);
  }

  void SendProfileUpdate(const std::string &bio, const std::string &pfp) {
    if (!connected)
      return;
    epyks::ProfileUpdate req;
    req.bio = bio;
    req.pfp_url = pfp;
    auto bytes = req.Serialize();
    epyks::Packet pkt;
    pkt.type = epyks::PacketType::PROFILE_UPDATE;
    pkt.data = std::string(bytes.begin(), bytes.end());
    auto data = pkt.Serialize();
    uint32_t len = (uint32_t)data.size();
    send(sock, (char *)&len, 4, 0);
    send(sock, (char *)data.data(), len, 0);
  }

  void RequestProfile(const std::string &target = "", bool silent = false) {
    if (!connected)
      return;
    pendingProfileRequest = target.empty() ? username : target;
    pendingProfileSilent = silent;
    epyks::Packet pkt;
    pkt.type = epyks::PacketType::GET_PROFILE;
    pkt.data = target;
    auto data = pkt.Serialize();
    uint32_t len = (uint32_t)data.size();
    send(sock, (char *)&len, 4, 0);
    send(sock, (char *)data.data(), len, 0);
  }

  // friends
  void SendFriendRequest(const std::string &target) {
    if (!connected)
      return;
    epyks::FriendRequest req;
    req.target_username = target;
    auto reqBytes = req.Serialize();

    epyks::Packet packet;
    packet.type = epyks::PacketType::FRIEND_REQUEST;
    packet.data = std::string(reqBytes.begin(), reqBytes.end());

    auto data = packet.Serialize();
    uint32_t len = (uint32_t)data.size();
    send(sock, (char *)&len, 4, 0);
    send(sock, (char *)data.data(), len, 0);
  }

  void RespondFriendRequest(const std::string &target, bool accept) {
    if (!connected)
      return;
    epyks::FriendResponse resp;
    resp.target_username = target;
    resp.accepted = accept;
    auto respBytes = resp.Serialize();

    epyks::Packet packet;
    packet.type = epyks::PacketType::FRIEND_RESPONSE;
    packet.data = std::string(respBytes.begin(), respBytes.end());

    auto data = packet.Serialize();
    uint32_t len = (uint32_t)data.size();
    send(sock, (char *)&len, 4, 0);
    send(sock, (char *)data.data(), len, 0);

    auto it = std::find(friendRequests.begin(), friendRequests.end(),
                        target + " wants to add you as friend");
    if (it != friendRequests.end())
      friendRequests.erase(it);
  }
  // private
  void RequestFriendList() {
    if (!connected)
      return;
    epyks::Packet packet;
    packet.type = epyks::PacketType::FRIEND_LIST;
    auto data = packet.Serialize();
    uint32_t len = (uint32_t)data.size();
    send(sock, (char *)&len, 4, 0);
    send(sock, (char *)data.data(), len, 0);
  }

  void SendListServers() {
    if (!connected)
      return;
    epyks::Packet packet;
    packet.type = epyks::PacketType::LIST_SERVERS;
    auto data = packet.Serialize();
    uint32_t len = (uint32_t)data.size();
    send(sock, (char *)&len, 4, 0);
    send(sock, (char *)data.data(), len, 0);
  }

  void GetMessages(std::vector<std::string> &out) {
    std::lock_guard<std::mutex> lock(msgMutex);
    out = messages;
  }

  // servers
  void SendCreateServer(const std::string &name,
                        const std::string &password = "") {
    if (!connected)
      return;
    epyks::CreateServer cg;
    cg.server_name = name;
    cg.password = password;
    auto pmBytes = cg.Serialize();
    epyks::Packet packet;
    packet.type = epyks::PacketType::CREATE_SERVER;
    packet.data = std::string(pmBytes.begin(), pmBytes.end());
    packet.timestamp = GetTickCount64();
    auto data = packet.Serialize();
    uint32_t len = (uint32_t)data.size();
    send(sock, (char *)&len, 4, 0);
    send(sock, (char *)data.data(), len, 0);
  }

  void TokenLogin(const std::string &u, const std::string &t) {
    if (!connected)
      return;
    username = u;
    epyks::TokenLoginRequest req;
    req.username = u;
    req.token = t;
    auto bytes = req.Serialize();
    epyks::Packet pkt;
    pkt.type = epyks::PacketType::TOKEN_LOGIN;
    pkt.data = std::string(bytes.begin(), bytes.end());
    auto data = pkt.Serialize();
    uint32_t len = (uint32_t)data.size();
    send(sock, (char *)&len, 4, 0);
    send(sock, (char *)data.data(), len, 0);
  }

  void SendJoinServer(int id, const std::string& pass = "") {
    if (!connected)
      return;
    epyks::JoinServer jg;
    jg.server_id = id;
    jg.password = pass;
    auto pmBytes = jg.Serialize();
    epyks::Packet packet;
    packet.type = epyks::PacketType::JOIN_SERVER;
    packet.data = std::string(pmBytes.begin(), pmBytes.end());
    packet.timestamp = GetTickCount64();
    auto data = packet.Serialize();
    uint32_t len = (uint32_t)data.size();
    send(sock, (char *)&len, 4, 0);
    send(sock, (char *)data.data(), len, 0);
  }

  void SendLeaveServer(int id) {
    if (!connected)
      return;
    epyks::LeaveServer lg;
    lg.server_id = id;
    auto pmBytes = lg.Serialize();
    epyks::Packet packet;
    packet.type = epyks::PacketType::LEAVE_SERVER;
    packet.data = std::string(pmBytes.begin(), pmBytes.end());
    packet.timestamp = GetTickCount64();
    auto data = packet.Serialize();
    uint32_t len = (uint32_t)data.size();
    send(sock, (char *)&len, 4, 0);
    send(sock, (char *)data.data(), len, 0);
  }

  void SendServerMessage(int serverId, int channelId,
                         const std::string &message) {
    if (!connected)
      return;
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
    send(sock, (char *)&len, 4, 0);
    send(sock, (char *)data.data(), len, 0);
  }

  void SendRenameServer(int serverId, const std::string &newName) {
    if (!connected)
      return;
    epyks::Packet packet;
    packet.type = epyks::PacketType::RENAME_SERVER;
    packet.data = std::to_string(serverId) + "|" + newName;
    auto data = packet.Serialize();
    uint32_t len = (uint32_t)data.size();
    send(sock, (char *)&len, 4, 0);
    send(sock, (char *)data.data(), len, 0);
  }

  void SendCreateChannel(int serverId, const std::string &name, int type, const std::string& category = "") {
    if (!connected)
      return;
    epyks::CreateChannel cc;
    cc.server_id = serverId;
    cc.channel_name = name;
    cc.type = type;
    cc.category = category;
    auto pmBytes = cc.Serialize();
    epyks::Packet packet;
    packet.type = epyks::PacketType::CREATE_CHANNEL;
    packet.data = std::string(pmBytes.begin(), pmBytes.end());
    auto data = packet.Serialize();
    uint32_t len = (uint32_t)data.size();
    send(sock, (char *)&len, 4, 0);
    send(sock, (char *)data.data(), len, 0);
  }

  void SendDeleteChannel(int serverId, int channelId) {
    if (!connected)
      return;
    epyks::DeleteChannel dc;
    dc.server_id = serverId;
    dc.channel_id = channelId;
    auto pmBytes = dc.Serialize();
    epyks::Packet packet;
    packet.type = epyks::PacketType::DELETE_CHANNEL;
    packet.data = std::string(pmBytes.begin(), pmBytes.end());
    auto data = packet.Serialize();
    uint32_t len = (uint32_t)data.size();
    send(sock, (char *)&len, 4, 0);
    send(sock, (char *)data.data(), len, 0);
  }

  void SendKickUser(int serverId, const std::string &username) {
    if (!connected)
      return;
    epyks::KickUser ku;
    ku.server_id = serverId;
    ku.target_username = username;
    auto pmBytes = ku.Serialize();
    epyks::Packet packet;
    packet.type = epyks::PacketType::KICK_USER;
    packet.data = std::string(pmBytes.begin(), pmBytes.end());
    auto data = packet.Serialize();
    uint32_t len = (uint32_t)data.size();
    send(sock, (char *)&len, 4, 0);
    send(sock, (char *)data.data(), len, 0);
  }

  void SendMuteUser(int serverId, const std::string &username, bool mute) {
    if (!connected)
      return;
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
    send(sock, (char *)&len, 4, 0);
    send(sock, (char *)data.data(), len, 0);
  }

  void RequestServerHistory(int serverId, int channelId) {
    if (!connected)
      return;
    epyks::Packet packet;
    packet.type = epyks::PacketType::HISTORY;
    packet.data = std::to_string(serverId) + "|" + std::to_string(channelId);
    auto data = packet.Serialize();
    uint32_t len = (uint32_t)data.size();
    send(sock, (char *)&len, 4, 0);
    send(sock, (char *)data.data(), len, 0);
  }

  void SendJoinVoice(int serverId, int channelId) {
    if (!connected)
      return;
    epyks::JoinVoice jv;
    jv.server_id = serverId;
    jv.channel_id = channelId;
    auto bytes = jv.Serialize();
    epyks::Packet packet;
    packet.type = epyks::PacketType::JOIN_VOICE;
    packet.data = std::string(bytes.begin(), bytes.end());
    auto data = packet.Serialize();
    uint32_t len = (uint32_t)data.size();
    send(sock, (char *)&len, 4, 0);
    send(sock, (char *)data.data(), len, 0);

    currentVoiceServerId = serverId;
    currentVoiceChannelId = channelId;
    inVoice = true;
    audio.StartVoice();

    // Send initial UDP packet to authenticate our endpoint
    SendVoiceData({});
  }

  void SendLeaveVoice() {
    if (!connected)
      return;
    epyks::LeaveVoice lv;
    lv.server_id = currentVoiceServerId;
    lv.channel_id = currentVoiceChannelId;
    auto bytes = lv.Serialize();
    epyks::Packet packet;
    packet.type = epyks::PacketType::LEAVE_VOICE;
    packet.data = std::string(bytes.begin(), bytes.end());
    auto data = packet.Serialize();
    uint32_t len = (uint32_t)data.size();
    send(sock, (char *)&len, 4, 0);
    send(sock, (char *)data.data(), len, 0);

    inVoice = false;
    audio.StopVoice();
    currentVoiceServerId = -1;
    currentVoiceChannelId = -1;
  }

  void SendVoiceData(const std::vector<uint8_t> &pcm) {
    if (udpSock == INVALID_SOCKET)
      return;
    epyks::VoiceData vd;
    vd.username = username;
    vd.server_id = currentVoiceServerId;
    vd.channel_id = currentVoiceChannelId;
    vd.audio_data = pcm;
    auto bytes = vd.Serialize();
    sendto(udpSock, (char *)bytes.data(), bytes.size(), 0,
           (sockaddr *)&serverUdpAddr, sizeof(serverUdpAddr));
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
        int bytes = recvfrom(udpSock, buffer, sizeof(buffer), 0,
                             (sockaddr *)&senderAddr, &senderLen);
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

  // conection
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
    if (recvThread.joinable())
      recvThread.join();
    if (udpThread.joinable())
      udpThread.join();
    audio.Shutdown();
  }
};

struct AppConfig {
  ImVec4 bgColor = ImVec4(0.21f, 0.22f, 0.25f, 1.0f); // #36393f (Chat Area)
  ImVec4 sidebarColor =
      ImVec4(0.12f, 0.13f, 0.14f, 1.0f); // #202225 (Server Sidebar)
  ImVec4 channelListColor =
      ImVec4(0.18f, 0.19f, 0.21f, 1.0f); // #2f3136 (Channel Sidebar)
  ImVec4 accentColor = ImVec4(0.34f, 0.39f, 0.95f, 1.0f); // #5865f2 (Blurple)

  ImVec4 textColor = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
  ImVec4 ownMessageColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
  ImVec4 otherMessageColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
  ImVec4 systemColor = ImVec4(1.0f, 0.8f, 0.4f, 1.0f);
  ImVec4 joinLeaveColor = ImVec4(0.5f, 1.0f, 0.5f, 1.0f);
  float fontSize = 16.0f;

  void Apply() {
    ImGuiStyle &style = ImGui::GetStyle();

    style.WindowRounding = 0.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 12.0f;
    style.GrabRounding = 5.0f;
    style.TabRounding = 5.0f;

    style.WindowPadding = ImVec2(0, 0);
    style.FramePadding = ImVec2(8, 6);
    style.ItemSpacing = ImVec2(8, 8);
    style.ScrollbarSize = 12.0f;

    style.Colors[ImGuiCol_WindowBg] = bgColor;
    style.Colors[ImGuiCol_ChildBg] =
        ImVec4(0, 0, 0, 0); // Transparent children, let parent background show
    style.Colors[ImGuiCol_Text] = textColor;
    style.Colors[ImGuiCol_Button] = accentColor;
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.38f, 0.44f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.30f, 0.34f, 0.85f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.16f, 0.18f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.19f, 0.22f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.12f, 0.13f, 0.15f, 1.0f);
    style.Colors[ImGuiCol_Header] =
        ImVec4(accentColor.x, accentColor.y, accentColor.z, 0.4f);
    style.Colors[ImGuiCol_HeaderHovered] =
        ImVec4(accentColor.x, accentColor.y, accentColor.z, 0.6f);
    style.Colors[ImGuiCol_HeaderActive] = accentColor;
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.12f, 0.13f, 0.14f, 0.0f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.08f, 0.08f, 0.09f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] =
        ImVec4(0.10f, 0.10f, 0.11f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabActive] =
        ImVec4(0.12f, 0.12f, 0.13f, 1.0f);
    style.Colors[ImGuiCol_Separator] = ImVec4(0.0f, 0.0f, 0.0f, 0.2f);
    style.Colors[ImGuiCol_TitleBg] = sidebarColor;
    style.Colors[ImGuiCol_TitleBgActive] = sidebarColor;
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.09f, 1.0f);
  }
};

void DrawHomeIcon(ImVec2 center, float size, ImU32 color) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    float h = size * 0.4f;
    float w = size * 0.4f;
    ImVec2 p[5] = {
        {center.x, center.y - h},
        {center.x - w, center.y},
        {center.x - w, center.y + h},
        {center.x + w, center.y + h},
        {center.x + w, center.y}
    };
    draw->AddPolyline(p, 5, color, ImDrawFlags_Closed, 2.0f);
}

void DrawBrowserIcon(ImVec2 center, float size, ImU32 color) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    float r = size * 0.45f;
    draw->AddCircle(center, r, color, 0, 2.0f);
    // Vertical "meridians"
    draw->AddBezierCubic(ImVec2(center.x, center.y - r), ImVec2(center.x - r * 0.5f, center.y - r * 0.5f), ImVec2(center.x - r * 0.5f, center.y + r * 0.5f), ImVec2(center.x, center.y + r), color, 1.5f);
    draw->AddBezierCubic(ImVec2(center.x, center.y - r), ImVec2(center.x + r * 0.5f, center.y - r * 0.5f), ImVec2(center.x + r * 0.5f, center.y + r * 0.5f), ImVec2(center.x, center.y + r), color, 1.5f);
    // Horizontal "equator"
    draw->AddLine(ImVec2(center.x - r, center.y), ImVec2(center.x + r, center.y), color, 1.5f);
}

void DrawAvatar(const std::string &username, const std::string &pfpUrl,
                float size) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 c = ImGui::GetCursorScreenPos();
  ImVec2 center = {c.x + size * 0.5f, c.y + size * 0.5f};
  float r = size * 0.5f;

  bool textureDrawn = false;
  if (!pfpUrl.empty()) {
      auto it = g_textureCache.find(pfpUrl);
      if (it == g_textureCache.end()) {
          // Try to load
          TextureInfo info = LoadTextureFromFile(pfpUrl.c_str(), g_pd3dDevice);
          if (info.srv) {
              g_textureCache[pfpUrl] = info;
              it = g_textureCache.find(pfpUrl);
          }
      }

      if (it != g_textureCache.end() && it->second.srv) {
          // Draw circle clipped texture
          dl->PathArcTo(center, r, 0, 3.14159f * 2.0f, 32);
          dl->PathFillConvex(IM_COL32(255, 255, 255, 255)); // Mask or background
          
          // Using ImGui::Image would be easier but we are in a custom DrawList
          // We'll use AddImageQuad or just AddImage with a circular clip if possible
          // For now, let's just do a simple square image with rounding
          dl->AddImageRounded(it->second.srv, c, {c.x + size, c.y + size}, {0,0}, {1,1}, IM_COL32_WHITE, r);
          textureDrawn = true;
      }
  }

  if (!textureDrawn) {
      // Placeholder circle with initial
      uint32_t h = 5381;
      for (char ch : username)
        h = ((h << 5) + h) + (unsigned char)ch;
      static ImU32 pal[] = {
          IM_COL32(88, 101, 242, 255), IM_COL32(87, 242, 135, 255),
          IM_COL32(254, 231, 92, 255), IM_COL32(235, 69, 158, 255),
          IM_COL32(0, 185, 255, 255),  IM_COL32(250, 119, 0, 255)};
      dl->AddCircleFilled(center, r, pal[h % 6]);

      if (!username.empty()) {
        char s[2] = {(char)toupper((unsigned char)username[0]), 0};
        ImVec2 ts = ImGui::CalcTextSize(s);
        dl->AddText({center.x - ts.x * .5f, center.y - ts.y * .5f}, IM_COL32_WHITE, s);
      }
  }

  ImGui::Dummy(ImVec2(size, size));
}

void DrawGearIcon(ImVec2 center, float size, ImU32 color) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  float r = size * 0.35f;
  float innerR = size * 0.12f;
  dl->AddCircle(center, r, color, 32, 2.0f);
  dl->AddCircleFilled(center, innerR, color);
  for (int i = 0; i < 8; i++) {
    float angle = i * (3.14159f * 2.0f / 8.0f);
    ImVec2 p1 = {center.x + cosf(angle) * r, center.y + sinf(angle) * r};
    ImVec2 p2 = {center.x + cosf(angle) * (r + size * 0.15f),
                 center.y + sinf(angle) * (r + size * 0.15f)};
    dl->AddLine(p1, p2, color, 2.5f);
  }
}

void DrawProfileModal(ChatClient &client, const epyks::UserProfile &profile,
                      bool *p_open) {
  ImGui::SetNextWindowSize(ImVec2(400, 500));
  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  if (ImGui::Begin("User Profile", p_open,
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.11f, 0.12f, 1.0f));
    ImGui::BeginChild("ProfileHeader", ImVec2(0, 150), true);
    ImGui::SetCursorPos(ImVec2(16, 40));
    DrawAvatar(profile.username, profile.pfp_url, 80);
    ImGui::SetCursorPos(ImVec2(110, 60));
    ImGui::Text("%s", profile.username.c_str());
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::SetCursorPosX(20);
    ImGui::BeginGroup();
    ImGui::TextDisabled("ABOUT ME");
    ImGui::PushTextWrapPos(380);
    ImGui::Text("%s", profile.bio.empty() ? "No bio set." : profile.bio.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();

    ImGui::SetCursorPos(ImVec2(20, 440));
    if (ImGui::Button("Close", ImVec2(360, 40)))
      *p_open = false;

    ImGui::End();
  }
}

std::string PickLocalFile() {
  char szFile[260] = {0};
  OPENFILENAMEA ofn;
  ZeroMemory(&ofn, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = NULL;
  ofn.lpstrFile = szFile;
  ofn.nMaxFile = sizeof(szFile);
  ofn.lpstrFilter = "Images\0*.png;*.jpg;*.jpeg;*.bmp\0All\0*.*\0";
  ofn.nFilterIndex = 1;
  ofn.lpstrFileTitle = NULL;
  ofn.nMaxFileTitle = 0;
  ofn.lpstrInitialDir = NULL;
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

  if (GetOpenFileNameA(&ofn) == TRUE) {
    return std::string(szFile);
  }
  return "";
}

static void DrawCallView(ChatClient& client) {
    // Periodically refresh member list for voice activity state
    static uint64_t lastRefresh = 0;
    uint64_t now = GetTickCount64();
    if (now - lastRefresh > 500) {
        if (client.currentServerId != -1) {
            client.RequestMemberList(client.currentServerId);
        }
        lastRefresh = now;
    }

    ImGui::BeginChild("CallViewContent", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1 = ImVec2(p0.x + ImGui::GetContentRegionAvail().x, p0.y + ImGui::GetContentRegionAvail().y);
    drawList->AddRectFilledMultiColor(p0, p1, ImColor(30, 31, 34), ImColor(30, 31, 34), ImColor(20, 21, 23), ImColor(20, 21, 23));

    ImGui::SetCursorPos(ImVec2(20, 20));
    ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "VOICE CONNECTED");
    
    // Participants Grid
    ImGui::SetCursorPos(ImVec2(50, 80));
    ImGui::BeginGroup();
    {
        float cardW = 200;
        float cardH = 240;
        float spacing = 20;
        int cols = (int)fmaxf(1, (ImGui::GetContentRegionAvail().x - 100) / (cardW + spacing));
        int count = 0;

        auto renderMember = [&](const std::string& name, const std::string& pfp, bool talking, bool muted, bool deafened, bool local) {
            ImGui::PushID(name.c_str());
            ImGui::BeginChild(name.c_str(), ImVec2(cardW, cardH), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
            ImDrawList* d = ImGui::GetWindowDrawList();
            ImVec2 cp = ImGui::GetCursorScreenPos();
            
            // Background
            d->AddRectFilled(cp, ImVec2(cp.x + cardW, cp.y + cardH), ImColor(43, 45, 49), 12.0f);
            
            // Talking Ring
            if (talking && !muted && !deafened) {
                d->AddRect(cp, ImVec2(cp.x + cardW, cp.y + cardH), ImColor(35, 165, 89), 12.0f, 0, 3.0f);
            }

            // Avatar
            ImGui::SetCursorPos(ImVec2(cardW*0.5f - 40, 90 - 40));
            DrawAvatar(name, pfp, 80);
            
            // Name
            ImGui::SetCursorPosY(170);
            std::string dispName = name + (local ? " (You)" : "");
            ImVec2 nSize = ImGui::CalcTextSize(dispName.c_str());
            ImGui::SetCursorPosX(cardW*0.5f - nSize.x * 0.5f);
            ImGui::TextUnformatted(dispName.c_str());

            // Status
            if (muted || deafened) {
                ImGui::SetCursorPosY(200);
                const char* statusTxt = deafened ? "DEAFENED" : "MUTED";
                ImVec2 mSize = ImGui::CalcTextSize(statusTxt);
                ImGui::SetCursorPosX(cardW*0.5f - mSize.x * 0.5f);
                ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), statusTxt);
            }
            ImGui::EndChild();
            ImGui::PopID();

            count++;
            if (count % cols != 0) ImGui::SameLine(0, spacing);
        };

        // 1. Render Local User
        bool localTalking = client.audio.IsSpeaking();
        renderMember(client.username, client.myProfile.pfp_url, localTalking, client.isMuted, client.isDeafened, true);

        // 2. Render Others in the same voice channel
        if (client.currentVoiceServerId != -1) {
            auto& members = client.serverMembers[client.currentVoiceServerId];
            for (auto& m : members) {
                if (m.username != client.username && m.voice_channel_id == client.currentVoiceChannelId) {
                    renderMember(m.username, m.pfp_url, m.is_talking, m.is_muted, false, false);
                }
            }
        }
    }
    ImGui::EndGroup();

    // Floating Toolbar at the bottom
    float toolbarW = 280;
    ImGui::SetCursorPos(ImVec2((ImGui::GetWindowWidth() - toolbarW) * 0.5f, ImGui::GetWindowHeight() - 80));
    
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 25.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.14f, 0.95f));
    ImGui::BeginChild("CallControls", ImVec2(toolbarW, 60), true, ImGuiWindowFlags_NoScrollbar);
    
    auto IconButton = [](const char* label, bool active, ImVec4 activeCol, const char* tooltip) {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 20.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, active ? activeCol : ImVec4(0.18f, 0.19f, 0.21f, 1.0f));
        ImGui::SetCursorPosY(10);
        bool pressed = ImGui::Button(label, ImVec2(75, 40));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
        ImGui::PopStyleColor(); ImGui::PopStyleVar();
        return pressed;
    };

    ImGui::SetCursorPosX(15);
    if (IconButton(client.isMuted ? "MIC OFF" : "MIC ON", client.isMuted, ImVec4(0.9f, 0.3f, 0.3f, 1.f), "Toggle Mic")) {
        client.isMuted = !client.isMuted;
        client.audio.SetMute(client.isMuted);
    }
    ImGui::SameLine(0, 8);
    if (IconButton(client.isDeafened ? "DEAF ON" : "DEAF OFF", client.isDeafened, ImVec4(0.9f, 0.3f, 0.3f, 1.f), "Toggle Deafen")) {
        client.isDeafened = !client.isDeafened;
        client.audio.SetDeafened(client.isDeafened);
    }
    ImGui::SameLine(0, 8);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 20.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.2f, 0.2f, 1.f));
    ImGui::SetCursorPosY(10);
    if (ImGui::Button("LEAVE", ImVec2(85, 40))) {
        int srvId = client.currentServerId;
        client.SendLeaveVoice();
        client.showVoiceCallWindow = false;
        // Switch to first text channel
        if (srvId != -1) {
            for (auto& c : client.servers[srvId].channels) {
                if (c.type == 0) {
                    client.currentChannelId = c.id;
                    break;
                }
            }
        }
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ImGui::EndChild();
}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
  WNDCLASSEX wc = {sizeof(wc), CS_CLASSDC,  WndProc,
                   0L,         0L,          GetModuleHandle(nullptr),
                   nullptr,    nullptr,     nullptr,
                   nullptr,    _T("Epyks"), nullptr};
  RegisterClassEx(&wc);
  HWND hwnd = CreateWindowEx(WS_EX_APPWINDOW, wc.lpszClassName, _T("Epyks"),
                           WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX, 
                           100, 100, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);

  // Enable shadow for borderless window
  const MARGINS shadow_margins = { 1, 1, 1, 1 };
  DwmExtendFrameIntoClientArea(hwnd, &shadow_margins);

  if (!CreateDeviceD3D(hwnd)) {
    CleanupDeviceD3D();
    UnregisterClass(wc.lpszClassName, wc.hInstance);
    return 1;
  }

  ShowWindow(hwnd, nCmdShow);
  UpdateWindow(hwnd);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  AppConfig config;
  config.Apply();

  ImGui_ImplWin32_Init(hwnd);
  ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

  // Clean Font
  ImFont *mainFont =
      io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
  if (!mainFont)
    io.Fonts->AddFontDefault();

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

  std::string savedInDev, savedOutDev, savedUser;
  if (LoadConfig(savedUser, client.sessionToken, savedInDev, savedOutDev)) {
    strcpy_s(usernameBuf, savedUser.c_str());
    triedAutoLogin = true;
    if (!savedInDev.empty() || !savedOutDev.empty()) {
      client.audio.SetDevices(savedInDev, savedOutDev);
    }
  }

  char inputBuf[256] = "";
  char addFriendBuf[64] = "";
  char createServerBuf[64] = "";
  char createChannelBuf[64] = "";
  int createChannelType = 0; // 0 for Text, 1 for Voice
  char kickMuteBuf[64] = "";
  char dmInputBuf[256] = "";

  // Profile editing
  char bioBuf[1024] = "";
  char pfpUrlBuf[512] = "";

  // Audio settings
  int selectedInputDevice = 0;
  int selectedOutputDevice = 0;
  std::vector<AudioDeviceInfo> inputDevices;
  std::vector<AudioDeviceInfo> outputDevices;

  std::vector<std::string> displayMessages;
  std::string currentDM;
  int settingsTab = 0; // 0: Account, 1: Profile, 2: Voice
  int serverSettingsTab = 0;
  bool showSettings = false;
  bool showAddFriend = false;
  bool showFriendRequests = false;
  bool showCreateServer = false;
  bool showBrowseServers = false;
  bool showCreateChannel = false;
  bool showCreateCategory = false;
  bool showKickMute = false;
  bool showServerSettings = false;
  static int friendsTab = 0;

  bool done = false;
  while (!done) {
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
      if (msg.message == WM_QUIT)
        done = true;
    }
    if (done)
      break;

    if (triedAutoLogin && !isConnecting) {
      isConnecting = true;
      if (client.Connect(serverIP, 9001)) {
        client.TokenLogin(usernameBuf, client.sessionToken);
      } else {
        isConnecting = false;
        triedAutoLogin = false;
      }
    }

    if (client.loginSuccess) {
      client.loginSuccess = false;

      if (client.loginErrorMsg == "REGISTER_SUCCESS") {
        showRegister = false;
        isConnecting = false;
        client.loginErrorMsg = "Registration successful! Please log in.";
        strcpy_s(usernameBuf, regUser);
        client.Disconnect();
      } else {

        showLogin = false;
        showRegister = false;
        isConnecting = false;
        triedAutoLogin = false;
        client.username = usernameBuf;

        std::string inDev, outDev;
        if (LoadConfig(client.username, client.sessionToken, inDev, outDev)) {
          if (!inDev.empty() || !outDev.empty()) {
            client.audio.SetDevices(inDev, outDev);
          }
        }

        if (rememberMe && !client.sessionToken.empty()) {
          SaveConfig(client.username, client.sessionToken, inDev, outDev);
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
      triedAutoLogin = false;
      showLogin = true;
      client.Disconnect();
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Custom Title Bar
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, 32));
        ImGui::Begin("TitleBar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        
        // Window Dragging handled via WM_NCHITTEST in WndProc for smoothness

        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(ImVec2(0, 0), ImVec2(io.DisplaySize.x, 32), ImColor(30, 31, 34));
        
        ImGui::SetCursorPos(ImVec2(10, 6));
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "EPYKS");
        
        float btnW = 46;
        ImGui::SetCursorPos(ImVec2(io.DisplaySize.x - btnW * 3, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
        
        if (ImGui::Button("_", ImVec2(btnW, 32))) ShowWindow(hwnd, SW_MINIMIZE);
        ImGui::SameLine(0, 0);
        if (ImGui::Button("[ ]", ImVec2(btnW, 32))) {
            if (IsZoomed(hwnd)) ShowWindow(hwnd, SW_RESTORE);
            else ShowWindow(hwnd, SW_MAXIMIZE);
        }
        ImGui::SameLine(0, 0);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("X", ImVec2(btnW, 32))) PostQuitMessage(0);
        
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
        ImGui::End();
    }

    ImGui::SetNextWindowPos(ImVec2(0, 32));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y - 32));

    if (showLogin || showRegister) {
      ImGui::PushStyleColor(ImGuiCol_WindowBg, config.sidebarColor);
      ImGui::Begin("LoginScreen", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

      float centerX = io.DisplaySize.x * 0.5f;
      float centerY = io.DisplaySize.y * 0.5f;
      ImGui::SetCursorPos(ImVec2(centerX - 200, centerY - 250));

      ImGui::PushStyleColor(ImGuiCol_ChildBg, config.bgColor);
      ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
      ImGui::BeginChild("LoginCard", ImVec2(400, 500), true,
                        ImGuiWindowFlags_NoScrollbar);

      ImGui::Dummy(ImVec2(0, 30));

      auto centerText = [](const char *text) {
        float width = ImGui::CalcTextSize(text).x;
        ImGui::SetCursorPosX((400 - width) * 0.5f);
        ImGui::Text("%s", text);
      };

      auto centerTextColored = [](const char *text, ImVec4 color) {
        float width = ImGui::CalcTextSize(text).x;
        ImGui::SetCursorPosX((400 - width) * 0.5f);
        ImGui::TextColored(color, "%s", text);
      };

      centerText("Welcome back!");
      centerTextColored("We're so excited to see you again!",
                        ImVec4(0.7f, 0.7f, 0.7f, 1.0f));

      ImGui::Dummy(ImVec2(0, 30));

      ImGui::PushItemWidth(340);
      if (showRegister) {
        ImGui::SetCursorPosX(30);
        ImGui::TextDisabled("USERNAME");
        ImGui::SetCursorPosX(30);
        ImGui::InputText("##reguser", regUser, 64);

        ImGui::Dummy(ImVec2(0, 15));
        ImGui::SetCursorPosX(30);
        ImGui::TextDisabled("PASSWORD");
        ImGui::SetCursorPosX(30);
        ImGui::InputText("##regpass", regPass, 64,
                         ImGuiInputTextFlags_Password);

        ImGui::Dummy(ImVec2(0, 15));
        ImGui::SetCursorPosX(30);
        ImGui::TextDisabled("CONFIRM PASSWORD");
        ImGui::SetCursorPosX(30);
        ImGui::InputText("##regconf", regConfirm, 64,
                         ImGuiInputTextFlags_Password);

        ImGui::Dummy(ImVec2(0, 25));
        ImGui::SetCursorPosX(30);
        if (ImGui::Button("Register", ImVec2(340, 45))) {
          if (strlen(regUser) >= 3 && strlen(regPass) >= 4 &&
              strcmp(regPass, regConfirm) == 0) {
            isConnecting = true;
            if (client.Connect(serverIP, 9001)) {
              client.Register(regUser, regPass);
            } else {
              isConnecting = false;
              client.loginErrorMsg = "Failed to connect";
            }
          } else {
            client.loginErrorMsg = "Invalid inputs or passwords mismatch";
          }
        }
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::SetCursorPosX(30);
        if (ImGui::Selectable("Already have an account?", false))
          showRegister = false;
      } else {
        ImGui::SetCursorPosX(30);
        ImGui::TextDisabled("USERNAME");
        ImGui::SetCursorPosX(30);
        ImGui::InputText("##user", usernameBuf, 64);

        ImGui::Dummy(ImVec2(0, 15));
        ImGui::SetCursorPosX(30);
        ImGui::TextDisabled("PASSWORD");
        ImGui::SetCursorPosX(30);
        ImGui::InputText("##pass", passwordBuf, 64,
                         ImGuiInputTextFlags_Password);

        ImGui::Dummy(ImVec2(0, 5));
        ImGui::SetCursorPosX(30);
        ImGui::Checkbox("Remember me", &rememberMe);

        ImGui::Dummy(ImVec2(0, 25));
        ImGui::SetCursorPosX(30);
        if (ImGui::Button("Log In", ImVec2(340, 45)) ||
            (ImGui::IsKeyPressed(ImGuiKey_Enter) && !isConnecting)) {
          isConnecting = true;
          if (client.Connect(serverIP, 9001)) {
            client.Login(usernameBuf, passwordBuf);
          } else {
            isConnecting = false;
            client.loginErrorMsg = "Failed to connect";
          }
        }
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::SetCursorPosX(30);
        if (ImGui::Selectable("Need an account? Register", false))
          showRegister = true;
      }
      ImGui::PopItemWidth();

      if (!client.loginErrorMsg.empty()) {
        ImGui::Dummy(ImVec2(0, 10));
        centerTextColored(client.loginErrorMsg.c_str(),
                          ImVec4(1, 0.4f, 0.4f, 1));
      }
      if (isConnecting) {
        ImGui::Dummy(ImVec2(0, 10));
        centerText("Connecting...");
      }

      ImGui::EndChild();
      ImGui::PopStyleVar();
      ImGui::PopStyleColor(2);
      ImGui::End();
    } else {
      ImGui::SetNextWindowPos(ImVec2(0, 32));
      ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y - 32));
      ImGui::Begin("Epyks Chat", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

      // Column 1: Server Sidebar
      ImGui::PushStyleColor(ImGuiCol_ChildBg, config.sidebarColor);
      ImGui::BeginChild("ServerSidebar", ImVec2(72, 0), false);

      // HM Button
      ImGui::SetCursorPos(ImVec2(11, 12));
      if (client.currentServerId == -1) {
        ImGui::PushStyleColor(ImGuiCol_Button, config.accentColor);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 16.0f);
      } else {
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImVec4(0.20f, 0.21f, 0.24f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 24.0f);
      }
      if (ImGui::Button("##HM", ImVec2(50, 50))) {
        client.currentServerId = -1;
        client.currentChannelId = -1;
        currentDM = "";
        client.showVoiceCallWindow = false;
      }
      ImVec2 hmCenter = ImVec2(ImGui::GetItemRectMin().x + 25, ImGui::GetItemRectMin().y + 25);
      DrawHomeIcon(hmCenter, 24, IM_COL32_WHITE);
      ImGui::PopStyleVar();
      ImGui::PopStyleColor();

      ImGui::SetCursorPosX(21);
      ImGui::Separator();

      // Server Icons
      for (auto &srvPair : client.servers) {
        int srvId = srvPair.first;
        ServerChat &srvData = srvPair.second;
        ImGui::SetCursorPosX(11);
        if (client.currentServerId == srvId) {
          ImGui::PushStyleColor(ImGuiCol_Button, config.accentColor);
          ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 16.0f);
        } else {
          ImGui::PushStyleColor(ImGuiCol_Button,
                                ImVec4(0.20f, 0.21f, 0.24f, 1.0f));
          ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 24.0f);
        }
        std::string initial = srvData.serverName.empty() ? "?" : srvData.serverName.substr(0, 2);
        if (ImGui::Button((initial + "##" + std::to_string(srvId)).c_str(),
                          ImVec2(50, 50))) {
          client.currentServerId = srvId;
          client.currentChannelId = -1;
          client.showVoiceCallWindow = false;
          currentDM = "";
          client.RequestMemberList(client.currentServerId);
          
          // Auto-select first text channel
          auto& srv = client.servers[srvId];
          for (auto& ch : srv.channels) {
              if (ch.type == 0) { // Text
                  client.currentChannelId = ch.id;
                  srv.channelMessages[ch.id].clear();
                  client.RequestServerHistory(srvId, ch.id);
                  break;
              }
          }
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("%s", srvData.serverName.c_str());
      }

      // Create Server Button
      ImGui::SetCursorPosX(11);
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.21f, 0.24f, 1.0f));
      if (ImGui::Button("+##create", ImVec2(50, 30)))
        showCreateServer = true;
      ImGui::PopStyleColor();
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Create a Server");

      // Browse Servers Button at the bottom
        ImGui::SetCursorPos(ImVec2(11, ImGui::GetWindowHeight() - 61));
        if (showBrowseServers) {
          ImGui::PushStyleColor(ImGuiCol_Button, config.accentColor);
          ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 16.0f);
        } else {
          ImGui::PushStyleColor(ImGuiCol_Button,
                                ImVec4(0.20f, 0.21f, 0.24f, 1.0f));
          ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 24.0f);
        }
        if (ImGui::Button("##B", ImVec2(50, 50))) {
          showBrowseServers = !showBrowseServers;
          if (showBrowseServers)
            client.SendListServers();
        }
        ImVec2 bCenter = ImVec2(ImGui::GetItemRectMin().x + 25, ImGui::GetItemRectMin().y + 25);
        DrawBrowserIcon(bCenter, 24, IM_COL32_WHITE);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Browse Servers");

      ImGui::EndChild();
      ImGui::PopStyleColor();
      ImGui::SameLine(0, 0);

      // Column 2: Channel List
      ImGui::PushStyleColor(ImGuiCol_ChildBg, config.channelListColor);
      ImGui::BeginChild("ChannelSidebar", ImVec2(240, 0), false);

      if (client.currentServerId == -1) {
        auto drawSelectable = [&](const char *label, bool selected, ImVec2 size) {
          ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
          ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));
          bool clicked = ImGui::Selectable(label, selected, 0, size);
          ImGui::PopStyleVar(2);
          return clicked;
        };

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::SetCursorPosX(8);
        if (drawSelectable("  Friends", currentDM == "friends_dashboard", ImVec2(224, 40))) {
            currentDM = "friends_dashboard";
            client.showVoiceCallWindow = false;
        }

        ImGui::Dummy(ImVec2(0, 16));
        ImGui::SetCursorPosX(16);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("DIRECT MESSAGES");
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 30);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.8f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 1.0f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0,0));
        if (ImGui::Button("+##adddm", ImVec2(20, 20))) {
            currentDM = "friends_dashboard";
            friendsTab = 2;
        }
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        ImGui::Separator();

        std::lock_guard<std::mutex> dmLock(client.friendsMutex);
        for (auto &friend_ : client.friends) {
          // Only show friends who have DM history or unread
          bool hasDM = client.dmChats.count(friend_.username) && !client.dmChats[friend_.username].messages.empty();
          if (!hasDM && !friend_.hasUnread) continue;
          ImVec4 color = friend_.hasUnread ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f)
                                           : config.textColor;
          ImGui::PushStyleColor(ImGuiCol_Text, color);
          std::string label = "  " + friend_.username + "##DM_" + friend_.username;
          if (drawSelectable(label.c_str(), currentDM == friend_.username,
                             ImVec2(208, 28))) {
            currentDM = friend_.username;
            friend_.hasUnread = false;
            client.showVoiceCallWindow = false;
          }
          ImGui::PopStyleColor();
        }

      } else {
        auto &server = client.servers[client.currentServerId];
        ImGui::SetCursorPos(ImVec2(16, 16));
        ImGui::Text("%s", server.serverName.c_str());
        ImGui::SameLine(205);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(1, 1, 1, 0.1f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              ImVec4(1, 1, 1, 0.2f));
        if (ImGui::Button("##serveropt", ImVec2(28, 28)))
          showServerSettings = true;
        ImVec2 gearPos = ImGui::GetItemRectMin();
        gearPos.x += 14;
        gearPos.y += 14;
        DrawGearIcon(gearPos, 14, IM_COL32_WHITE);
        ImGui::PopStyleColor(3);
        ImGui::Separator();

        // Categories
        auto renderChannelCategory = [&](const std::string &title, const std::string& category) {
          ImGui::Dummy(ImVec2(0, 8));
          ImGui::SetCursorPosX(16);
          ImGui::TextDisabled("%s", title.c_str());
          
          for (int i = 0; i < (int)server.channels.size(); ++i) {
            auto &channel = server.channels[i];
            if (channel.category != category && !(category == "Uncategorized" && channel.category.empty())) continue;
            if (channel.type == 2) continue; // Category header

            ImGui::SetCursorPosX(16);
            std::string label = (channel.type == 0 ? "# " : "v ") +
                                channel.name + "##CH_" +
                                std::to_string(channel.id);
            if (ImGui::Selectable(label.c_str(), client.currentChannelId == channel.id,
                                  0, ImVec2(0, 32))) {
              client.currentChannelId = channel.id;
              if (channel.type == 0) { // Text
                server.channelMessages[channel.id].clear();
                client.RequestServerHistory(client.currentServerId, channel.id);
                client.showVoiceCallWindow = false;
              }
              if (channel.type == 1) { // Voice
                client.audio.StartVoice();
                client.SendJoinVoice(client.currentServerId, channel.id);
                client.showVoiceCallWindow = true;
              }
            }
          }
        };

        std::vector<std::string> cats;
        for(auto& ch : server.channels) {
            std::string catName = (ch.type == 2) ? ch.name : ch.category;
            if (catName.empty()) catName = "Uncategorized";
            bool found = false;
            for(auto& c : cats) if(c == catName) { found = true; break; }
            if(!found) cats.push_back(catName);
        }

        for(auto& cat : cats) {
            renderChannelCategory(cat, cat);
        }
      }

      // User Summary
      ImGui::SetCursorPos(ImVec2(10, ImGui::GetWindowHeight() - 62));
      ImGui::BeginChild("UserSummary", ImVec2(220, 52), true,
                        ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoScrollWithMouse);
      ImGui::PushStyleColor(ImGuiCol_ChildBg,
                            ImVec4(0.14f, 0.15f, 0.16f, 1.0f));

      ImGui::SetCursorPos(ImVec2(8, 10));
      DrawAvatar(client.username, client.myProfile.pfp_url, 32);
      ImGui::SameLine(48);
      ImGui::BeginGroup();
      ImGui::Text("%s", client.username.c_str());
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, -2));
      ImGui::SetWindowFontScale(0.85f);
      ImGui::TextDisabled("Online");
      ImGui::SetWindowFontScale(1.0f);
      ImGui::PopStyleVar();
      ImGui::EndGroup();
      ImGui::SameLine(175);
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                            ImVec4(1, 1, 1, 0.1f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                            ImVec4(1, 1, 1, 0.2f));
      if (ImGui::Button("##settings", ImVec2(32, 32))) {
        showSettings = true;
        strncpy_s(bioBuf, sizeof(bioBuf), client.myProfile.bio.c_str(), _TRUNCATE);
        strncpy_s(pfpUrlBuf, sizeof(pfpUrlBuf), client.myProfile.pfp_url.c_str(), _TRUNCATE);
        inputDevices = client.audio.GetInputDevices();
        outputDevices = client.audio.GetOutputDevices();
        // Pre-select current devices
        std::string curIn = client.audio.GetCurrentInputDevice();
        std::string curOut = client.audio.GetCurrentOutputDevice();
        selectedInputDevice = 0;
        selectedOutputDevice = 0;
        for (int i = 0; i < (int)inputDevices.size(); i++)
          if (inputDevices[i].name == curIn) { selectedInputDevice = i; break; }
        for (int i = 0; i < (int)outputDevices.size(); i++)
          if (outputDevices[i].name == curOut) { selectedOutputDevice = i; break; }
      }
      ImVec2 uGearPos = ImGui::GetItemRectMin();
      uGearPos.x += 16;
      uGearPos.y += 16;
      DrawGearIcon(uGearPos, 14, IM_COL32_WHITE);
      ImGui::PopStyleColor(3);
      ImGui::PopStyleColor();
      ImGui::EndChild();

      ImGui::EndChild();
      ImGui::PopStyleColor();
      ImGui::SameLine(0, 0);

      // Column 3: Chat Area
      float memberSidebarWidth = (client.currentServerId != -1) ? 240.0f : 0.0f;
      ImGui::BeginChild(
          "ChatArea",
          ImVec2(ImGui::GetContentRegionAvail().x - memberSidebarWidth, 0),
          false);

      if (client.inVoice && client.showVoiceCallWindow) {
          DrawCallView(client);
      } else if (client.currentServerId == -1 && currentDM == "friends_dashboard") {
          if (client.inVoice) {
              ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.14f, 0.95f));
              ImGui::SetCursorPos(ImVec2(10, 6));
              ImGui::BeginChild("VoicePill", ImVec2(ImGui::GetContentRegionAvail().x - 20, 36), true, ImGuiWindowFlags_NoScrollbar);
              ImGui::SetCursorPos(ImVec2(10, 8));
              ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.f), "Voice Active: %s", client.currentVoiceServerId != -1 ? client.servers[client.currentVoiceServerId].serverName.c_str() : "Private Call");
              ImGui::SameLine(ImGui::GetWindowWidth() - 110);
              ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
              if (ImGui::Button("Open Call", ImVec2(100, 22))) client.showVoiceCallWindow = true;
              ImGui::PopStyleVar();
              ImGui::EndChild();
              ImGui::PopStyleColor();
              ImGui::Dummy(ImVec2(0, 4));
          }
          ImGui::BeginChild("FriendsDashboard", ImVec2(0, 0), false);
          
          // Header Tabs
          ImGui::Dummy(ImVec2(0, 10));
          ImGui::SetCursorPosX(20);
          ImGui::TextDisabled("FRIENDS");
          ImGui::SameLine();
          ImGui::SetCursorPosX(100);
          
          auto miniTab = [&](const char *lbl, int tab) {
            bool active = friendsTab == tab;
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, config.accentColor);
            else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 4));
            if (ImGui::Button(lbl)) friendsTab = tab;
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            ImGui::SameLine();
          };
          miniTab("All", 0);
          miniTab("Pending", 1);
          miniTab("Add Friend", 2);
          ImGui::NewLine();
          ImGui::Separator();
          
          ImGui::Dummy(ImVec2(0, 10));
          std::lock_guard<std::mutex> lock(client.friendsMutex);
          
          if (friendsTab == 0) {
            for (auto &f : client.friends) {
              ImGui::SetCursorPosX(20);
              DrawAvatar(f.username, "", 32);
              ImGui::SameLine(60);
              ImGui::Text("%s", f.username.c_str());
              ImGui::SameLine(ImGui::GetContentRegionAvail().x - 120);
              ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
              if (ImGui::Button(("Message##dm_" + f.username).c_str())) {
                currentDM = f.username;
                f.hasUnread = false;
              }
              ImGui::PopStyleColor();
              if (ImGui::BeginPopupContextItem(("ctx_" + f.username).c_str())) {
                if (ImGui::MenuItem("View Profile")) client.RequestProfile(f.username);
                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.3f, 0.3f, 1));
                if (ImGui::MenuItem("Unfriend")) {
                  client.SendUnfriend(f.username);
                  if (currentDM == f.username) currentDM = "";
                  client.RequestFriendList();
                }
                ImGui::PopStyleColor();
                ImGui::EndPopup();
              }
              ImGui::Separator();
            }
            if (client.friends.empty()) {
              ImGui::SetCursorPosX(20);
              ImGui::TextDisabled("No friends yet");
            }
          } else if (friendsTab == 1) {
            for (auto &req : client.friendRequests) {
              ImGui::SetCursorPosX(20);
              ImGui::Text("%s", req.c_str());
              ImGui::SameLine(ImGui::GetContentRegionAvail().x - 160);
              ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.49f, 0.20f, 1.0f));
              if (ImGui::Button(("Accept##" + req).c_str()))
                client.RespondFriendRequest(req, true);
              ImGui::PopStyleColor();
              ImGui::SameLine();
              ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
              if (ImGui::Button(("Decline##" + req).c_str()))
                client.RespondFriendRequest(req, false);
              ImGui::PopStyleColor();
              ImGui::Separator();
            }
            if (client.friendRequests.empty()) {
              ImGui::SetCursorPosX(20);
              ImGui::TextDisabled("No pending requests");
            }
          } else if (friendsTab == 2) {
            ImGui::SetCursorPosX(20);
            ImGui::Text("Add Friend");
            ImGui::SetCursorPosX(20);
            ImGui::TextDisabled("You can add friends with their username.");
            ImGui::Dummy(ImVec2(0, 10));
            ImGui::SetCursorPosX(20);
            ImGui::PushItemWidth(400);
            ImGui::InputText("##addtag", addFriendBuf, 64);
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, config.accentColor);
            if (ImGui::Button("Send Friend Request")) {
              if (strlen(addFriendBuf) > 0) {
                client.SendFriendRequest(addFriendBuf);
                addFriendBuf[0] = '\0';
              }
            }
            ImGui::PopStyleColor();
          }
          ImGui::EndChild();
      } else if (client.currentServerId == -1 && currentDM.empty()) {
        // Empty state - no DM selected
        float cx = ImGui::GetContentRegionAvail().x * 0.5f;
        float cy = ImGui::GetContentRegionAvail().y * 0.4f;
        ImGui::SetCursorPos(ImVec2(cx - 100, cy));
        ImGui::TextDisabled("Select a friend to start chatting");
      } else if (client.currentServerId != -1 && client.currentChannelId != -1) {
        auto &server = client.servers[client.currentServerId];
        std::string channelName = "Unknown";
        int channelType = 0;
        for (auto &c : server.channels) {
          if (c.id == client.currentChannelId) {
            channelName = c.name;
            channelType = c.type;
          }
        }

        ImGui::SetCursorPos(ImVec2(16, 16));
        ImGui::Text("%s %s", channelType == 0 ? "#" : "v", channelName.c_str());
        ImGui::Separator();

        if (client.inVoice && !client.showVoiceCallWindow) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.14f, 0.95f));
            ImGui::SetCursorPos(ImVec2(10, 40));
            ImGui::BeginChild("VoicePill", ImVec2(ImGui::GetContentRegionAvail().x - 20, 42), true, ImGuiWindowFlags_NoScrollbar);
            ImGui::SetCursorPos(ImVec2(10, 11));
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.f), "Voice Active: %s", client.currentVoiceServerId != -1 ? client.servers[client.currentVoiceServerId].serverName.c_str() : "Private Call");
            ImGui::SameLine(ImGui::GetWindowWidth() - 110);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            if (ImGui::Button("Open Call", ImVec2(100, 26))) client.showVoiceCallWindow = true;
            ImGui::PopStyleVar();
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, 4));
        }

        if (channelType == 0) { // Text Channel
          float chatHeight = ImGui::GetContentRegionAvail().y - 60;
          ImGui::BeginChild("ChannelHistory", ImVec2(0, chatHeight), false);
          auto &messages = server.channelMessages[client.currentChannelId];
          for (auto &m : messages) {
            ImGui::BeginGroup();
            ImGui::SetCursorPosX(16);
            
            std::string user = m.sender;
            std::string content = m.content;
            std::string timeStr = "Today";

            // Parse out "[HH:MM] [username] " if present
            if (content.size() > 7 && content[0] == '[' && content[6] == ']') {
                timeStr = content.substr(1, 5);
                size_t secondClose = content.find(']', 7);
                if (secondClose != std::string::npos) {
                    content = content.substr(secondClose + 2);
                }
            }
            
            std::string pfp = "";
            if (client.userProfileCache.count(user)) {
                pfp = client.userProfileCache[user].pfp_url;
            } else {
                for(auto& mi : client.serverMembers[client.currentServerId]) {
                  if(mi.username == user) { pfp = mi.pfp_url; break; }
                }
            }

            DrawAvatar(user, pfp, 40);
            ImGui::SameLine(64);
            ImGui::BeginGroup();
            ImGui::TextColored(config.accentColor, "%s", user.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", timeStr.c_str());
            ImGui::TextWrapped("%s", content.c_str());
            ImGui::EndGroup();
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                client.RequestProfile(user);
            }
            ImGui::EndGroup();
            ImGui::Dummy(ImVec2(0, 12));
          }
          if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
          ImGui::EndChild();

          ImGui::SetCursorPos(ImVec2(16, ImGui::GetWindowHeight() - 48));
          ImGui::PushItemWidth(-16);
          if (ImGui::InputText("##channelinput", inputBuf, 256,
                               ImGuiInputTextFlags_EnterReturnsTrue)) {
            client.SendServerMessage(client.currentServerId, client.currentChannelId,
                                     inputBuf);
            inputBuf[0] = '\0';
            ImGui::SetKeyboardFocusHere(-1);
          }
          ImGui::PopItemWidth();
        } else if (channelType == 1) { // Voice Channel
          ImGui::SetCursorPos(ImVec2(100, 100));
          if (client.inVoice &&
              client.currentVoiceServerId == client.currentServerId &&
              client.currentVoiceChannelId == client.currentChannelId) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Voice Connected");
          } else {
            if (ImGui::Button("Join Voice", ImVec2(150, 40)))
              client.SendJoinVoice(client.currentServerId, client.currentChannelId);
          }
        }
      } else if (client.currentServerId == -1 && !currentDM.empty()) {
        ImGui::SetCursorPos(ImVec2(16, 16));
        ImGui::Text("@ %s", currentDM.c_str());
        ImGui::Separator();

        if (client.inVoice && !client.showVoiceCallWindow) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.14f, 0.95f));
            ImGui::SetCursorPos(ImVec2(10, 40));
            ImGui::BeginChild("VoicePill", ImVec2(ImGui::GetContentRegionAvail().x - 20, 42), true, ImGuiWindowFlags_NoScrollbar);
            ImGui::SetCursorPos(ImVec2(10, 11));
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.f), "Voice Active: %s", client.currentVoiceServerId != -1 ? client.servers[client.currentVoiceServerId].serverName.c_str() : "Private Call");
            ImGui::SameLine(ImGui::GetWindowWidth() - 110);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            if (ImGui::Button("Open Call", ImVec2(100, 26))) client.showVoiceCallWindow = true;
            ImGui::PopStyleVar();
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, 4));
        }

        float chatHeight = ImGui::GetContentRegionAvail().y - 60;
        ImGui::BeginChild("DMHistory", ImVec2(0, chatHeight), false);
        auto &dm = client.dmChats[currentDM];
        for (auto &m : dm.messages) {
          ImGui::BeginGroup();
          ImGui::SetCursorPosX(16);
          bool isMe = m.find("You:") == 0;
          std::string user = isMe ? client.username : currentDM;
          std::string content = isMe ? m.substr(5) : m;
          DrawAvatar(user, "", 40);
          ImGui::SameLine(64);
          ImGui::BeginGroup();
          ImGui::TextColored(isMe ? config.accentColor : ImVec4(1, 1, 1, 1),
                             "%s", user.c_str());
          ImGui::TextWrapped("%s", content.c_str());
          ImGui::EndGroup();
          ImGui::EndGroup();
          ImGui::Dummy(ImVec2(0, 8));
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
          ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();

        ImGui::SetCursorPos(ImVec2(16, ImGui::GetWindowHeight() - 48));
        ImGui::PushItemWidth(-16);
        if (ImGui::InputText("##dminput", dmInputBuf, 256,
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
          client.SendDM(currentDM, dmInputBuf);
          dmInputBuf[0] = '\0';
          ImGui::SetKeyboardFocusHere(-1);
        }
        ImGui::PopItemWidth();
      } else {
        if (client.inVoice && !client.showVoiceCallWindow) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.14f, 0.95f));
            ImGui::SetCursorPos(ImVec2(10, 10));
            ImGui::BeginChild("VoicePill", ImVec2(ImGui::GetContentRegionAvail().x - 20, 42), true, ImGuiWindowFlags_NoScrollbar);
            ImGui::SetCursorPos(ImVec2(10, 11));
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.f), "Voice Active: %s", client.currentVoiceServerId != -1 ? client.servers[client.currentVoiceServerId].serverName.c_str() : "Private Call");
            ImGui::SameLine(ImGui::GetWindowWidth() - 110);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            if (ImGui::Button("Open Call", ImVec2(100, 26))) client.showVoiceCallWindow = true;
            ImGui::PopStyleVar();
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
      }

      ImGui::EndChild();

      // Column 4: Member List
      if (client.currentServerId != -1) {
        ImGui::SameLine(0, 0);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, config.channelListColor);
        ImGui::BeginChild("MemberSidebar", ImVec2(240, 0), false);
        ImGui::SetCursorPos(ImVec2(16, 16));
        ImGui::TextDisabled("MEMBERS - %d",
                            (int)client.serverMembers[client.currentServerId].size());
        ImGui::Separator();
        auto &members = client.serverMembers[client.currentServerId];
        for (auto &m : members) {
          ImGui::SetCursorPosX(16);
          std::string selId = "##msel_" + m.username;
          bool clicked = ImGui::Selectable(selId.c_str(), false, 0, ImVec2(0, 40));
          ImGui::SameLine(16);
          DrawAvatar(m.username, m.pfp_url, 32);
          ImGui::SameLine(56);
          ImGui::BeginGroup();
          ImGui::Text("%s", m.username.c_str());
          if (!m.bio.empty())
            ImGui::TextDisabled("%s", m.bio.c_str());
          ImGui::EndGroup();
          if (clicked) {
            client.RequestProfile(m.username);
          }
          ImGui::Dummy(ImVec2(0, 4));
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
      }

      ImGui::End();


      // Popups & Modals
      ImVec2 center = ImGui::GetMainViewport()->GetCenter();

      if (showSettings) {
        ImGuiIO &io2 = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io2.DisplaySize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        if (ImGui::Begin("Settings##win", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
          // Top bar with close button
          ImGui::SetCursorPos(ImVec2(0, 0));
          ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.09f, 0.10f, 1.0f));
          ImGui::BeginChild("SettingsTopBar", ImVec2(0, 48), false, ImGuiWindowFlags_NoScrollbar);
          ImGui::SetCursorPos(ImVec2(io2.DisplaySize.x - 110, 10));
          ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.15f, 0.15f, 1.0f));
          if (ImGui::Button("  ESC  Close", ImVec2(100, 28))) showSettings = false;
          ImGui::PopStyleColor();
          ImGui::EndChild();
          ImGui::PopStyleColor();
          if (ImGui::IsKeyPressed(ImGuiKey_Escape)) showSettings = false;

          // Two-column layout
          ImGui::PushStyleColor(ImGuiCol_ChildBg, config.channelListColor);
          ImGui::BeginChild("SettingsSidebar", ImVec2(220, 0), true);
          ImGui::Dummy(ImVec2(0, 20));

          auto setSelectable = [&](const char *label, int tab) {
            ImGui::SetCursorPosX(20);
            if (ImGui::Selectable(label, settingsTab == tab, 0,
                                  ImVec2(180, 34)))
              settingsTab = tab;
          };

          ImGui::SetCursorPosX(20);
          ImGui::TextDisabled("USER SETTINGS");
          setSelectable("My Account", 0);
          setSelectable("Profiles", 1);
          ImGui::Dummy(ImVec2(0, 8));
          ImGui::SetCursorPosX(20);
          ImGui::TextDisabled("APP SETTINGS");
          setSelectable("Voice & Video", 2);
          ImGui::EndChild();
          ImGui::PopStyleColor();

          ImGui::SameLine();
          ImGui::BeginChild("SettingsContent", ImVec2(0, 0), false);
          ImGui::Dummy(ImVec2(0, 40));
          ImGui::SetCursorPosX(30);
          ImGui::BeginGroup();

          if (settingsTab == 0) {
            ImGui::Text("Account Settings");
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 10));
            ImGui::Text("Username");
            ImGui::TextDisabled("%s", client.username.c_str());
            ImGui::Dummy(ImVec2(0, 20));
            if (ImGui::Button("Clear Saved Credentials", ImVec2(200, 35)))
              ClearCredentials();

            ImGui::SetCursorPos(ImVec2(30, 520));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            if (ImGui::Button("Logout", ImVec2(120, 35))) {
              ClearCredentials();
              client.Disconnect();
              showLogin = true;
              showSettings = false;
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
          } else if (settingsTab == 1) {
            ImGui::Text("User Profile");
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 10));
            ImGui::Text("Bio");
            ImGui::InputTextMultiline("##bio", bioBuf, 1024, ImVec2(500, 100));
            ImGui::Dummy(ImVec2(0, 10));
            ImGui::Text("PFP URL or Local File");
            ImGui::PushItemWidth(380);
            ImGui::InputText("##pfp", pfpUrlBuf, 512);
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button("Browse...")) {
                std::string path = PickLocalFile();
                if (!path.empty()) {
                    strncpy_s(pfpUrlBuf, 512, path.c_str(), _TRUNCATE);
                }
            }
            ImGui::Dummy(ImVec2(0, 20));
            if (ImGui::Button("Save Profile", ImVec2(120, 40))) {
              client.SendProfileUpdate(bioBuf, pfpUrlBuf);
              client.myProfile.pfp_url = pfpUrlBuf;
              client.myProfile.bio = bioBuf;
            }
          } else if (settingsTab == 2) {
            ImGui::Text("Voice & Video");
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 10));
            ImGui::Text("Input Device");
            ImGui::PushItemWidth(400);
            if (ImGui::BeginCombo("##inputdev",
                    inputDevices.empty() ? "No devices" : inputDevices[selectedInputDevice].name.c_str())) {
              for (int i = 0; i < (int)inputDevices.size(); ++i)
                if (ImGui::Selectable(inputDevices[i].name.c_str(), selectedInputDevice == i))
                  selectedInputDevice = i;
              ImGui::EndCombo();
            }
            ImGui::Dummy(ImVec2(0, 10));
            ImGui::Text("Output Device");
            if (ImGui::BeginCombo("##outputdev",
                    outputDevices.empty() ? "No devices" : outputDevices[selectedOutputDevice].name.c_str())) {
              for (int i = 0; i < (int)outputDevices.size(); ++i)
                if (ImGui::Selectable(outputDevices[i].name.c_str(), selectedOutputDevice == i))
                  selectedOutputDevice = i;
              ImGui::EndCombo();
            }
            ImGui::PopItemWidth();
            ImGui::Dummy(ImVec2(0, 20));
            if (ImGui::Button("Apply Devices", ImVec2(150, 40))) {
              client.audio.SetDevices(inputDevices[selectedInputDevice].name,
                                      outputDevices[selectedOutputDevice].name);
              SaveConfig(client.username, client.sessionToken,
                         inputDevices[selectedInputDevice].name,
                         outputDevices[selectedOutputDevice].name);
            }
          }
          ImGui::EndGroup();
          ImGui::EndChild();
          ImGui::End();
        }
        ImGui::PopStyleVar(2);
      }

      if (showServerSettings && client.currentServerId != -1) {
        ImGuiIO &io2 = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io2.DisplaySize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        if (ImGui::Begin("Server Settings##win", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
          // Top bar
          ImGui::SetCursorPos(ImVec2(0, 0));
          ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.09f, 0.10f, 1.0f));
          ImGui::BeginChild("SrvSettTopBar", ImVec2(0, 48), false, ImGuiWindowFlags_NoScrollbar);
          ImGui::SetCursorPos(ImVec2(io2.DisplaySize.x - 110, 10));
          ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.15f, 0.15f, 1.0f));
          if (ImGui::Button("  ESC  Close##srv", ImVec2(100, 28))) { showServerSettings = false; client.lastServerOpStatus = ""; }
          ImGui::PopStyleColor();
          ImGui::EndChild();
          ImGui::PopStyleColor();
          if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { showServerSettings = false; client.lastServerOpStatus = ""; }

          ImGui::BeginChild("SrvSettSidebar", ImVec2(220, 0), true);
          auto srvSelect = [&](const char *label, int tab) {
            if (ImGui::Selectable(label, serverSettingsTab == tab, 0,
                                  ImVec2(0, 34))) {
              serverSettingsTab = tab;
              client.lastServerOpStatus = "";
            }
          };
          srvSelect("Overview", 0);
          srvSelect("Channels", 1);
          srvSelect("Members", 2);
          ImGui::EndChild();

          ImGui::SameLine();
          ImGui::BeginChild("SrvSettContent", ImVec2(0, 0), false);
          ImGui::Dummy(ImVec2(0, 20));
          ImGui::SetCursorPosX(20);
          ImGui::BeginGroup();

          auto &server = client.servers[client.currentServerId];
          if (serverSettingsTab == 0) {
            ImGui::Text("Server Overview");
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 20));
            ImGui::Text("Server Name");
            static int lastSrvId = -1;
            static char srvNameEdit[64] = "";
            if (lastSrvId != client.currentServerId) {
              strcpy_s(srvNameEdit, server.serverName.c_str());
              lastSrvId = client.currentServerId;
            }
            ImGui::InputText("##srvname", srvNameEdit, 64);
            if (ImGui::Button("Save Changes")) {
              client.SendRenameServer(client.currentServerId, srvNameEdit);
            }
            if (!client.lastServerOpStatus.empty()) {
                ImGui::Dummy(ImVec2(0, 10));
                ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "%s", client.lastServerOpStatus.c_str());
            }
          } else if (serverSettingsTab == 1) {
            static bool showInlineChannelForm = false;
            static bool inlineFormIsCategory = false;
            static char inlineChName[64] = "";
            static int inlineChType = 0;
            static int inlineChCatIdx = 0;

            ImGui::Text("Channel Management");
            ImGui::Separator();
            std::vector<std::string> catOpts = {"(None)"};
            for (auto &ch : server.channels) {
                if (ch.type == 2) {
                    bool dup = false;
                    for (auto &x : catOpts) if (x == ch.name) { dup = true; break; }
                    if (!dup) catOpts.push_back(ch.name);
                }
            }
            ImGui::Dummy(ImVec2(0, 10));
            if (ImGui::Button("+ Create Channel", ImVec2(170, 35))) {
              showInlineChannelForm = true;
              inlineFormIsCategory = false;
              inlineChName[0] = '\0';
              inlineChType = 0;
              inlineChCatIdx = 0;
            }
            ImGui::SameLine();
            if (ImGui::Button("+ Create Category", ImVec2(170, 35))) {
              showInlineChannelForm = true;
              inlineFormIsCategory = true;
              inlineChName[0] = '\0';
            }

            if (showInlineChannelForm) {
              ImGui::Dummy(ImVec2(0, 8));
              ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.13f, 0.15f, 1.0f));
              ImGui::BeginChild("InlineForm", ImVec2(0, inlineFormIsCategory ? 110 : 180), true);
              ImGui::Dummy(ImVec2(0, 6));
              ImGui::SetCursorPosX(12);
              ImGui::Text(inlineFormIsCategory ? "Category Name" : "Channel Name");
              ImGui::SetCursorPosX(12);
              ImGui::PushItemWidth(440);
              ImGui::InputText("##ilname", inlineChName, 64);
              ImGui::PopItemWidth();

              if (!inlineFormIsCategory) {
                ImGui::Dummy(ImVec2(0, 4));
                ImGui::SetCursorPosX(12);
                ImGui::Text("Type");
                ImGui::SetCursorPosX(12);
                ImGui::PushItemWidth(440);
                ImGui::Combo("##iltype", &inlineChType, "Text\0Voice\0");
                ImGui::PopItemWidth();

                if (inlineChCatIdx >= (int)catOpts.size()) inlineChCatIdx = 0;
                ImGui::Dummy(ImVec2(0, 4));
                ImGui::SetCursorPosX(12);
                ImGui::Text("Category");
                ImGui::SetCursorPosX(12);
                ImGui::PushItemWidth(440);
                if (ImGui::BeginCombo("##ilcat", catOpts[inlineChCatIdx].c_str())) {
                  for (int i = 0; i < (int)catOpts.size(); i++)
                    if (ImGui::Selectable(catOpts[i].c_str(), inlineChCatIdx == i))
                      inlineChCatIdx = i;
                  ImGui::EndCombo();
                }
                ImGui::PopItemWidth();
              }

              ImGui::Dummy(ImVec2(0, 8));
              ImGui::SetCursorPosX(12);
              if (ImGui::Button("Create##il", ImVec2(100, 30))) {
                client.lastServerOpStatus = "Sending request...";
                if (strlen(inlineChName) > 0) {
                  if (inlineFormIsCategory) {
                    client.SendCreateChannel(client.currentServerId, inlineChName, 2, inlineChName);
                  } else {
                    std::vector<std::string> catOpts2 = {"(None)"};
                    for (auto &ch : server.channels) {
                      std::string c = (ch.type == 2) ? ch.name : ch.category;
                      if (!c.empty()) {
                        bool dup = false;
                        for (auto &x : catOpts2) if (x == c) { dup = true; break; }
                        if (!dup) catOpts2.push_back(c);
                      }
                    }
                    std::string cat = (inlineChCatIdx == 0) ? "" : catOpts2[inlineChCatIdx];
                    client.SendCreateChannel(client.currentServerId, inlineChName, inlineChType, cat);
                  }
                  inlineChName[0] = '\0';
                  showInlineChannelForm = false;
                }
              }
              ImGui::SameLine();
              if (ImGui::Button("Cancel##il", ImVec2(100, 30))) {
                showInlineChannelForm = false;
                inlineChName[0] = '\0';
              }
              ImGui::EndChild();
              ImGui::PopStyleColor();
            }

            ImGui::Dummy(ImVec2(0, 10));
            for (int i = 0; i < (int)server.channels.size(); ++i) {
              auto &ch = server.channels[i];
              ImGui::Text("%s", ch.name.c_str());
              ImGui::SameLine(300);
              if (ImGui::Button(("Edit##" + std::to_string(ch.id)).c_str())) {
                  showInlineChannelForm = true;
                  inlineFormIsCategory = false;
                  strcpy_s(inlineChName, ch.name.c_str());
                  inlineChType = ch.type;
                  // Set category if it belongs to one
                  for(int idx=0; idx < (int)catOpts.size(); idx++) if(catOpts[idx] == ch.category) inlineChCatIdx = idx;
              }
              ImGui::SameLine();
              if (ImGui::Button(("Delete##" + std::to_string(ch.id)).c_str())) {
                client.SendDeleteChannel(client.currentServerId, ch.id);
                server.channels.erase(server.channels.begin() + i);
                break;
              }
            }
          } else if (serverSettingsTab == 2) {
            ImGui::Text("Member Management");
            ImGui::Separator();
            auto &members = client.serverMembers[client.currentServerId];
            for (auto &m : members) {
              ImGui::Text("%s", m.username.c_str());
              if (m.username != client.username) {
                ImGui::SameLine(300);
                if (ImGui::Button(("Kick##" + m.username).c_str())) {
                  client.SendKickUser(client.currentServerId, m.username);
                }
              }
            }
          }

          ImGui::Dummy(ImVec2(0, 40));
          ImGui::Separator();
          ImGui::Dummy(ImVec2(0, 10));
          if (ImGui::IsKeyPressed(ImGuiKey_Escape)) showServerSettings = false;
          ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
          if (ImGui::Button("Leave Server", ImVec2(200, 35))) {
            ImGui::OpenPopup("Confirm Leave");
          }
          if (ImGui::BeginPopupModal("Confirm Leave", nullptr,
                                     ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Are you sure you want to leave?");
            if (ImGui::Button("Leave", ImVec2(100, 30))) {
              client.SendLeaveServer(client.currentServerId);
              showServerSettings = false;
              ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 30))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
          }
          ImGui::PopStyleColor();

          ImGui::EndGroup();
          ImGui::EndChild();
          ImGui::End();
        }
        ImGui::PopStyleVar(2);
      }

      if (showAddFriend)
        ImGui::OpenPopup("Add Friend");
      ImGui::SetNextWindowSize(ImVec2(400, 200));
      if (ImGui::BeginPopupModal("Add Friend", &showAddFriend,
                                 ImGuiWindowFlags_NoResize)) {
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::SetCursorPosX(30);
        ImGui::TextDisabled("ENTER USERNAME");
        ImGui::SetCursorPosX(30);
        ImGui::PushItemWidth(340);
        ImGui::InputText("##friend", addFriendBuf, 64);
        ImGui::PopItemWidth();
        ImGui::Dummy(ImVec2(0, 20));
        ImGui::SetCursorPosX(30);
        if (ImGui::Button("Send Request", ImVec2(120, 35))) {
          client.SendFriendRequest(addFriendBuf);
          addFriendBuf[0] = '\0';
          showAddFriend = false;
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 35))) {
          showAddFriend = false;
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }

      if (showFriendRequests)
        ImGui::OpenPopup("Friend Requests");
      ImGui::SetNextWindowSize(ImVec2(450, 350));
      if (ImGui::BeginPopupModal("Friend Requests", &showFriendRequests,
                                 ImGuiWindowFlags_NoResize)) {
        ImGui::BeginChild("ReqsList", ImVec2(0, 250), true);
        for (auto &req : client.friendRequests) {
          ImGui::Text("%s", req.c_str());
          ImGui::SameLine(ImGui::GetWindowWidth() - 150);
          if (ImGui::Button(("Accept##" + req).c_str()))
            client.RespondFriendRequest(req, true);
          ImGui::SameLine();
          if (ImGui::Button(("Decline##" + req).c_str()))
            client.RespondFriendRequest(req, false);
        }
        if (client.friendRequests.empty())
          ImGui::Text("No pending requests");
        ImGui::EndChild();
        ImGui::SetCursorPos(ImVec2(20, 300));
        if (ImGui::Button("Close", ImVec2(100, 30))) {
          showFriendRequests = false;
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }

      if (showCreateServer)
        ImGui::OpenPopup("Create Server");
      ImGui::SetNextWindowSize(ImVec2(400, 300));
      if (ImGui::BeginPopupModal("Create Server", &showCreateServer,
                                 ImGuiWindowFlags_NoResize)) {
        ImGui::Dummy(ImVec2(0, 20));
        ImGui::SetCursorPosX(30);
        ImGui::TextDisabled("SERVER NAME");
        ImGui::SetCursorPosX(30);
        ImGui::InputText("##createserver", createServerBuf, 64);

        ImGui::Dummy(ImVec2(0, 10));
        ImGui::SetCursorPosX(30);
        ImGui::TextDisabled("PASSWORD (OPTIONAL)");
        static char srvPass[64] = "";
        ImGui::SetCursorPosX(30);
        ImGui::InputText("##srvpass", srvPass, 64,
                         ImGuiInputTextFlags_Password);

        ImGui::Dummy(ImVec2(0, 30));
        ImGui::SetCursorPosX(30);
        if (ImGui::Button("Create", ImVec2(100, 40))) {
          client.SendCreateServer(createServerBuf, srvPass);
          createServerBuf[0] = '\0';
          srvPass[0] = '\0';
          showCreateServer = false;
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 40))) {
          showCreateServer = false;
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }

      if (client.showProfileModal) {
          DrawProfileModal(client, client.selectedProfile, &client.showProfileModal);
      }

      if (showBrowseServers)
        ImGui::OpenPopup("Browse Servers");
      ImGui::SetNextWindowSize(ImVec2(500, 400));
      if (ImGui::BeginPopupModal("Browse Servers", &showBrowseServers,
                                 ImGuiWindowFlags_NoResize)) {
        ImGui::Dummy(ImVec2(0, 10));
        static bool sentReq = false;
        if (!sentReq) {
          client.SendListServers();
          sentReq = true;
        }
        for (auto &s : client.availableServers) {
          int sid = std::get<0>(s);
          std::string &sname = std::get<1>(s);
          bool hasPass = std::get<2>(s);

          ImGui::SetCursorPosX(20);
          ImGui::Text("%s", sname.c_str());
          if (hasPass) {
              ImGui::SameLine();
              ImGui::TextDisabled("(Locked)");
          }
          ImGui::SameLine(400);
          if (ImGui::Button(("Join##" + std::to_string(sid)).c_str())) {
              if (hasPass) {
                  client.targetServerToJoin = sid;
                  client.showPasswordModal = true;
                  client.serverPassBuf[0] = '\0';
              } else {
                  client.SendJoinServer(sid);
              }
          }
          ImGui::Separator();
        }
        ImGui::SetCursorPos(ImVec2(20, 350));
        if (ImGui::Button("Close", ImVec2(100, 30))) {
          showBrowseServers = false;
          sentReq = false;
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }

      if (showCreateChannel)
        ImGui::OpenPopup("Create Channel");
      ImGui::SetNextWindowSize(ImVec2(400, 280));
      if (ImGui::BeginPopupModal("Create Channel", &showCreateChannel,
                                 ImGuiWindowFlags_NoResize)) {
        static char chName[64] = "";
        static int chType = 0;
        static int selectedCatIdx = 0;

        // Build category list from current server
        std::vector<std::string> catOptions;
        catOptions.push_back("(None)");
        if (client.currentServerId != -1) {
          auto &srv = client.servers[client.currentServerId];
          for (auto &ch : srv.channels) {
            std::string cat = (ch.type == 2) ? ch.name : ch.category;
            if (!cat.empty()) {
              bool found = false;
              for (auto &c : catOptions) if (c == cat) { found = true; break; }
              if (!found) catOptions.push_back(cat);
            }
          }
        }
        if (selectedCatIdx >= (int)catOptions.size()) selectedCatIdx = 0;

        ImGui::Dummy(ImVec2(0, 10));
        ImGui::SetCursorPosX(20);
        ImGui::Text("Channel Name");
        ImGui::SetCursorPosX(20);
        ImGui::PushItemWidth(360);
        ImGui::InputText("##chname", chName, 64);
        ImGui::PopItemWidth();

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::SetCursorPosX(20);
        ImGui::Text("Type");
        ImGui::SetCursorPosX(20);
        ImGui::PushItemWidth(360);
        ImGui::Combo("##chtype", &chType, "Text\0Voice\0");
        ImGui::PopItemWidth();

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::SetCursorPosX(20);
        ImGui::Text("Category");
        ImGui::SetCursorPosX(20);
        ImGui::PushItemWidth(360);
        if (ImGui::BeginCombo("##chcat", catOptions[selectedCatIdx].c_str())) {
          for (int i = 0; i < (int)catOptions.size(); ++i) {
            if (ImGui::Selectable(catOptions[i].c_str(), selectedCatIdx == i))
              selectedCatIdx = i;
          }
          ImGui::EndCombo();
        }
        ImGui::PopItemWidth();

        ImGui::Dummy(ImVec2(0, 15));
        ImGui::SetCursorPosX(20);
        if (ImGui::Button("Create", ImVec2(120, 35))) {
          std::string cat = (selectedCatIdx == 0) ? "" : catOptions[selectedCatIdx];
          client.SendCreateChannel(client.currentServerId, chName, chType, cat);
          showCreateChannel = false;
          chName[0] = '\0';
          selectedCatIdx = 0;
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 35))) {
          showCreateChannel = false;
          chName[0] = '\0';
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }

      if (showCreateCategory)
        ImGui::OpenPopup("Create Category");
      ImGui::SetNextWindowSize(ImVec2(400, 180));
      if (ImGui::BeginPopupModal("Create Category", &showCreateCategory,
                                 ImGuiWindowFlags_NoResize)) {
        static char catName[64] = "";
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::SetCursorPosX(20);
        ImGui::Text("Category Name");
        ImGui::SetCursorPosX(20);
        ImGui::PushItemWidth(360);
        ImGui::InputText("##catname", catName, 64);
        ImGui::PopItemWidth();

        ImGui::Dummy(ImVec2(0, 15));
        ImGui::SetCursorPosX(20);
        if (ImGui::Button("Create", ImVec2(120, 35))) {
          if (strlen(catName) > 0) {
            client.SendCreateChannel(client.currentServerId, catName, 2, catName);
          }
          showCreateCategory = false;
          catName[0] = '\0';
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 35))) {
          showCreateCategory = false;
          catName[0] = '\0';
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }

      if (showKickMute)
        ImGui::OpenPopup("Moderate Members");
      ImGui::SetNextWindowSize(ImVec2(400, 250));
      if (ImGui::BeginPopupModal("Moderate Members", &showKickMute,
                                 ImGuiWindowFlags_NoResize)) {
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::SetCursorPosX(30);
        ImGui::TextDisabled("TARGET USERNAME");
        ImGui::SetCursorPosX(30);
        ImGui::InputText("##targetuser", kickMuteBuf, 64);
        ImGui::Dummy(ImVec2(0, 20));
        ImGui::SetCursorPosX(30);
        if (ImGui::Button("Kick", ImVec2(80, 35))) {
          client.SendKickUser(client.currentServerId, kickMuteBuf);
          showKickMute = false;
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Mute", ImVec2(80, 35))) {
          client.SendMuteUser(client.currentServerId, kickMuteBuf, true);
          showKickMute = false;
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Unmute", ImVec2(80, 35))) {
          client.SendMuteUser(client.currentServerId, kickMuteBuf, false);
          showKickMute = false;
          ImGui::CloseCurrentPopup();
        }
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::SetCursorPosX(30);
        if (ImGui::Button("Cancel", ImVec2(340, 35))) {
          showKickMute = false;
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }
    }

      if (client.showPasswordModal) {
          ImGui::OpenPopup("Enter Password");
          client.showPasswordModal = false;
      }

      if (ImGui::BeginPopupModal("Enter Password", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
          ImGui::Text("This server is password protected.");
          ImGui::InputText("Password", client.serverPassBuf, 64, ImGuiInputTextFlags_Password);
          if (ImGui::Button("Join", ImVec2(120, 0))) {
              client.SendJoinServer(client.targetServerToJoin, client.serverPassBuf);
              ImGui::CloseCurrentPopup();
              showBrowseServers = false;
          }
          ImGui::SameLine();
          if (ImGui::Button("Cancel", ImVec2(120, 0))) {
              ImGui::CloseCurrentPopup();
          }
          ImGui::EndPopup();
      }


    ImGui::Render();
    const float clear_color[4] = {config.bgColor.x, config.bgColor.y,
                                  config.bgColor.z, config.bgColor.w};
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView,
                                            nullptr);
    g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView,
                                               clear_color);
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
  const D3D_FEATURE_LEVEL featureLevelArray[2] = {D3D_FEATURE_LEVEL_11_0,
                                                  D3D_FEATURE_LEVEL_10_0};
  if (D3D11CreateDeviceAndSwapChain(
          nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
          featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
          &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK)
    return false;

  CreateRenderTarget();
  return true;
}

void CleanupDeviceD3D() {
  CleanupRenderTarget();
  if (g_pSwapChain) {
    g_pSwapChain->Release();
    g_pSwapChain = nullptr;
  }
  if (g_pd3dDeviceContext) {
    g_pd3dDeviceContext->Release();
    g_pd3dDeviceContext = nullptr;
  }
  if (g_pd3dDevice) {
    g_pd3dDevice->Release();
    g_pd3dDevice = nullptr;
  }
}

void CreateRenderTarget() {
  ID3D11Texture2D *pBackBuffer;
  g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
  g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr,
                                       &g_mainRenderTargetView);
  pBackBuffer->Release();
}

void CleanupRenderTarget() {
  if (g_mainRenderTargetView) {
    g_mainRenderTargetView->Release();
    g_mainRenderTargetView = nullptr;
  }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
                                                             UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
    return true;
  switch (msg) {
  case WM_NCCALCSIZE:
      return 0;
  case WM_NCHITTEST: {
      LRESULT hit = DefWindowProc(hWnd, msg, wParam, lParam);
      if (hit == HTCLIENT) {
          POINT pt = { LOWORD(lParam), HIWORD(lParam) };
          ScreenToClient(hWnd, &pt);
          RECT rc; GetClientRect(hWnd, &rc);
          if (pt.y < 32 && pt.x < (rc.right - 140)) return HTCAPTION;
      }
      return hit;
  }
  case WM_SIZE:
    if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
      CleanupRenderTarget();
      g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam),
                                  DXGI_FORMAT_UNKNOWN, 0);
      CreateRenderTarget();
    }
    return 0;
  case WM_SYSCOMMAND:
    if ((wParam & 0xfff0) == SC_KEYMENU)
      return 0;
    break;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProc(hWnd, msg, wParam, lParam);
}