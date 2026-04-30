#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#define GetTickCount64()                                                       \
  (std::chrono::duration_cast<std::chrono::milliseconds>(                      \
       std::chrono::steady_clock::now().time_since_epoch())                    \
       .count())
#endif

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>

void LogToFile(const std::string &message) {
  std::ofstream file("server_log.txt", std::ios::app);
  if (file.is_open()) {
    auto now =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm ltm;
    localtime_s(&ltm, &now);
    file << "[" << std::put_time(&ltm, "%Y-%m-%d %H:%M:%S") << "] " << message
         << std::endl;
  }
}
#include "Database.h"
#include "Protocol/Packet.h"
#include "ServerGUI.h"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct Client {
  SOCKET socket = INVALID_SOCKET;
  std::string username;
  bool hasUsername = false;
  bool authenticated = false;

  // Voice tracking
  int currentVoiceServerId = -1;
  int currentVoiceChannelId = -1;
  sockaddr_in udpAddr = {};
  bool hasUdpAddr = false;
  uint64_t lastVoicePacket = 0;
};

class ChatServer {
  SOCKET listenSocket = INVALID_SOCKET;
  std::atomic<bool> running{false};
  std::atomic<bool> accepting{false};
  std::vector<std::thread> threads;
  std::vector<Client> clients;
  std::mutex clientsMutex;
  uint64_t nextUserId = 0;
  ServerGUI *gui = nullptr;
  Database *db = nullptr;
  int port = 9001;

  SOCKET udpSocket = INVALID_SOCKET;
  std::thread udpThread;

public:
  void SetGUI(ServerGUI *g) { gui = g; }
  void SetDatabase(Database *d) { db = d; }

  bool Start(int port) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
      return false;
#endif

    listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocket == INVALID_SOCKET)
      return false;

    int opt = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt,
               sizeof(opt));

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(listenSocket, (sockaddr *)&serverAddr, sizeof(serverAddr)) ==
        SOCKET_ERROR) {
      closesocket(listenSocket);
#ifdef _WIN32
      WSACleanup();
#endif
      return false;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
      closesocket(listenSocket);
#ifdef _WIN32
      WSACleanup();
#endif
      return false;
    }

    running = true;
    accepting = true;

    udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket != INVALID_SOCKET) {
      int optUdp = 1;
      setsockopt(udpSocket, SOL_SOCKET, SO_REUSEADDR, (char *)&optUdp,
                 sizeof(optUdp));
      if (bind(udpSocket, (sockaddr *)&serverAddr, sizeof(serverAddr)) ==
          SOCKET_ERROR) {
        if (gui)
          gui->AddLog("[Error] Failed to bind UDP port " +
                      std::to_string(port));
        closesocket(udpSocket);
        udpSocket = INVALID_SOCKET;
      } else {
        if (gui)
          gui->AddLog("UDP Server listening on port " + std::to_string(port));
        udpThread = std::thread(&ChatServer::UdpLoop, this);
      }
    }

    threads.emplace_back(&ChatServer::AcceptLoop, this);
    printf("Server listening on port %d\n", port);
    return true;
  }

  void Stop() {
    accepting = false;
    running = false;
    closesocket(listenSocket);
    if (udpSocket != INVALID_SOCKET)
      closesocket(udpSocket);

    {
      std::lock_guard<std::mutex> lock(clientsMutex);
      for (auto &c : clients) {
        closesocket(c.socket);
      }
      clients.clear();
    }

    for (auto &t : threads) {
      if (t.joinable())
        t.join();
    }
    if (udpThread.joinable())
      udpThread.join();

#ifdef _WIN32
    WSACleanup();
#endif
  }

  void AcceptLoop() {
    while (accepting) {
      sockaddr_in clientAddr;
#ifdef _WIN32
      int addrLen = sizeof(clientAddr);
#else
      socklen_t addrLen = sizeof(clientAddr);
#endif
      SOCKET clientSocket =
          accept(listenSocket, (sockaddr *)&clientAddr, &addrLen);

      if (clientSocket == INVALID_SOCKET) {
        if (accepting && gui)
          gui->AddLog("[Error] Accept failed");
        else if (accepting)
          printf("[Error] Accept failed\n");
        continue;
      }

      char ip[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &clientAddr.sin_addr, ip, INET_ADDRSTRLEN);
      std::cout << "[System] New connection accepted" << std::endl;
      LogToFile("New connection accepted from client.");
      if (gui)
        gui->AddLog("Connection from " + std::string(ip));
      else
        printf("Connection from %s\n", ip);

      {
        std::lock_guard<std::mutex> lock(clientsMutex);
        Client newClient;
        newClient.socket = clientSocket;
        newClient.hasUsername = false;
        newClient.authenticated = false;
        clients.push_back(newClient);
      }

      threads.emplace_back(&ChatServer::HandleClient, this, clientSocket);
    }
  }

  void UdpLoop() {
    char buffer[4096];
    sockaddr_in senderAddr;
    int senderLen = sizeof(senderAddr);

    while (running) {
      int bytes = recvfrom(udpSocket, buffer, sizeof(buffer), 0,
                           (sockaddr *)&senderAddr, &senderLen);
      if (bytes > 0) {
        std::vector<uint8_t> data(buffer, buffer + bytes);
        epyks::VoiceData pkt;
        if (pkt.Deserialize(data)) {
          std::lock_guard<std::mutex> lock(clientsMutex);

          bool senderVerified = false;
          for (auto &c : clients) {
            if (c.authenticated && c.username == pkt.username &&
                c.currentVoiceServerId == pkt.server_id &&
                c.currentVoiceChannelId == pkt.channel_id) {
              c.udpAddr = senderAddr;
              c.hasUdpAddr = true;
              c.lastVoicePacket = GetTickCount64();
              senderVerified = true;
              break;
            }
          }

          if (senderVerified) {
            for (auto &c : clients) {
              if (c.authenticated && c.hasUdpAddr &&
                  c.username != pkt.username &&
                  c.currentVoiceServerId == pkt.server_id &&
                  c.currentVoiceChannelId == pkt.channel_id) {
                sendto(udpSocket, buffer, bytes, 0, (sockaddr *)&c.udpAddr,
                       sizeof(c.udpAddr));
              }
            }
          }
        }
      }
    }
  }

  void HandleClient(SOCKET sock) {
    bool authenticated = false;
    std::string username;
    std::string sessionToken;

    while (running && !authenticated) {
      epyks::Packet packet;
      if (!ReceivePacket(sock, packet)) {
        RemoveClient(sock);
        return;
      }

      if (packet.type == epyks::PacketType::REGISTER) {
        epyks::RegisterRequest req;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (req.Deserialize(bytes) && db) {
          epyks::RegisterResponse resp;
          if (db->AccountExists(req.username)) {
            resp.success = false;
            resp.error = "Username already exists";
          } else if (req.username.length() < 3) {
            resp.success = false;
            resp.error = "Username must be at least 3 characters";
          } else if (req.password.length() < 4) {
            resp.success = false;
            resp.error = "Password must be at least 4 characters";
          } else {
            db->CreateAccountSecure(req.username, req.password);
            resp.success = true;
            if (gui)
              gui->AddLog("[Auth] Account created: " + req.username);
            LogToFile("[Auth] Account created: " + req.username);
          }
          auto respBytes = resp.Serialize();
          epyks::Packet response;
          response.type = epyks::PacketType::REGISTER_RESPONSE;
          response.data = std::string(respBytes.begin(), respBytes.end());
          SendTo(sock, response);
        }
      } else if (packet.type == epyks::PacketType::LOGIN) {
        epyks::LoginRequest req;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (req.Deserialize(bytes) && db) {
          epyks::LoginResponse resp;
          if (db->ValidateLoginSecure(req.username, req.password)) {
            authenticated = true;
            username = req.username;
            resp.success = true;

            sessionToken = db->GenerateSalt() + db->GenerateSalt();
            db->SaveSessionToken(username, sessionToken);
            resp.session_token = sessionToken;
            if (gui)
              gui->AddLog("[Auth] Login success: " + req.username);
            LogToFile("[Auth] Login success: " + req.username);

            // Send pending friend requests to the newly logged-in user
            auto pendingRequests = db->GetPendingFriendRequests(req.username);
            for (const auto& sender : pendingRequests) {
              epyks::Packet notify;
              notify.type = epyks::PacketType::FRIEND_REQUEST;
              notify.data = sender + " wants to add you as friend";
              SendTo(sock, notify);
            }
            // Send DM contacts
            {
              auto contacts = db->GetDMContacts(req.username);
              std::string dmPayload;
              for (auto &c : contacts) dmPayload += c + ",";
              epyks::Packet dmsPkt;
              dmsPkt.type = epyks::PacketType::MY_DMS;
              dmsPkt.data = dmPayload;
              SendTo(sock, dmsPkt);
            }
          } else {
            resp.success = false;
            resp.error = "Invalid username or password";
            if (gui)
              gui->AddLog("[Auth] Login failed: " + req.username);
            LogToFile("[Auth] Login failed: " + req.username);
          }
          auto respBytes = resp.Serialize();
          epyks::Packet response;
          response.type = epyks::PacketType::LOGIN_RESPONSE;
          response.data = std::string(respBytes.begin(), respBytes.end());
          SendTo(sock, response);
        }
      } else if (packet.type == epyks::PacketType::TOKEN_LOGIN) {
        epyks::TokenLoginRequest req;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (req.Deserialize(bytes) && db) {
          epyks::LoginResponse resp;
          if (db->ValidateSessionToken(req.username, req.token)) {
            authenticated = true;
            username = req.username;
            resp.success = true;

            sessionToken = db->GenerateSalt() + db->GenerateSalt();
            db->SaveSessionToken(username, sessionToken);
            resp.session_token = sessionToken;
            if (gui)
              gui->AddLog("[Auth] Token login success: " + req.username);
            LogToFile("[Auth] Token login success: " + req.username);

            // Send pending friend requests to the newly logged-in user
            auto pendingRequests = db->GetPendingFriendRequests(req.username);
            for (const auto& sender : pendingRequests) {
              epyks::Packet notify;
              notify.type = epyks::PacketType::FRIEND_REQUEST;
              notify.data = sender + " wants to add you as friend";
              SendTo(sock, notify);
            }
            // Send DM contacts
            {
              auto contacts = db->GetDMContacts(req.username);
              std::string dmPayload;
              for (auto &c : contacts) dmPayload += c + ",";
              epyks::Packet dmsPkt;
              dmsPkt.type = epyks::PacketType::MY_DMS;
              dmsPkt.data = dmPayload;
              SendTo(sock, dmsPkt);
            }
          } else {
            resp.success = false;
            resp.error = "Session expired";
            if (gui)
              gui->AddLog("[Auth] Token login failed: " + req.username);
            LogToFile("[Auth] Token login failed: " + req.username);
          }
          auto respBytes = resp.Serialize();
          epyks::Packet response;
          response.type = epyks::PacketType::TOKEN_LOGIN_RESPONSE;
          response.data = std::string(respBytes.begin(), respBytes.end());
          SendTo(sock, response);
        }
      }
    }

    {
      std::lock_guard<std::mutex> lock(clientsMutex);
      for (auto &c : clients) {
        if (c.socket == sock) {
          c.username = username;
          c.hasUsername = true;
          c.authenticated = true;
          break;
        }
      }
    }

    if (gui)
      gui->AddLog("[" + username + "] logged in");
    LogToFile("[" + username + "] logged in");

    if (db) {
      auto messages = db->GetRecentMessages(100);
      for (auto &msg : messages) {
        epyks::Packet hist;
        hist.type = epyks::PacketType::HISTORY;
        hist.data = "[" + msg.username + "]: " + msg.message;
        hist.timestamp = msg.timestamp;
        SendTo(sock, hist);
      }
    }

    epyks::Packet joinMsg;
    joinMsg.type = epyks::PacketType::JOIN_LEAVE;
    joinMsg.data = username + " joined the chat";
    Broadcast(joinMsg, sock);

    while (running) {
      epyks::Packet packet;
      if (!ReceivePacket(sock, packet))
        break;

      if (packet.type == epyks::PacketType::HISTORY) {
        try {
          size_t sep = packet.data.find('|');
          if (sep != std::string::npos && db) {
            std::string sidStr = packet.data.substr(0, sep);
            std::string cidStr = packet.data.substr(sep + 1);
            if (sidStr.empty() || cidStr.empty()) {
              if (gui)
                gui->AddLog("[Error] Malformed history request: " +
                            packet.data);
            } else {
              int serverId = std::stoi(sidStr);
              int channelId = std::stoi(cidStr);
              auto history = db->GetServerMessages(serverId, channelId, 100);
              for (auto &msg : history) {
                epyks::ServerMessage sm;
                sm.server_id = serverId;
                sm.channel_id = channelId;
                sm.username = msg.username;
                sm.content = msg.message;
                sm.message_id = msg.id;
                sm.reply_to_id = msg.reply_to_id;

                epyks::Packet hist;
                hist.type = epyks::PacketType::SERVER_MESSAGE;
                auto sdata = sm.Serialize();
                hist.data = std::string(sdata.begin(), sdata.end());
                SendTo(sock, hist);
              }
            }
          } else if (packet.data.find("DM:") == 0 && db) {
            std::string target = packet.data.substr(3);
            auto history = db->GetPrivateMessages(username, target, 100);
            for (auto &msg : history) {
              epyks::PrivateMessage pm;
              pm.target_username = target;
              pm.sender_username = msg.username;
              pm.content = msg.message;
              pm.message_id = msg.id;
              pm.reply_to_id = msg.reply_to_id;

              epyks::Packet hist;
              hist.type = epyks::PacketType::PRIVATE_MESSAGE;
              auto pdata = pm.Serialize();
              hist.data = std::string(pdata.begin(), pdata.end());
              SendTo(sock, hist);
            }
          } else if (db) {
            auto messages = db->GetRecentMessages(100);
            for (auto &msg : messages) {
              epyks::Packet hist;
              hist.type = epyks::PacketType::HISTORY;
              hist.data = "[" + msg.username + "]: " + msg.message;
              hist.timestamp = msg.timestamp;
              SendTo(sock, hist);
            }
          }
        } catch (const std::exception &e) {
          if (gui)
            gui->AddLog("[Error] History request error: " +
                        std::string(e.what()));
        }
      } else if (packet.type == epyks::PacketType::CHAT_MESSAGE) {
        if (gui)
          gui->AddLog("[" + username + "]: " + packet.data);
        if (db)
          db->SaveMessage(username, packet.data, packet.timestamp);

        epyks::Packet broadcast;
        broadcast.type = epyks::PacketType::CHAT_MESSAGE;
        broadcast.data = "[" + username + "]: " + packet.data;
        broadcast.timestamp = packet.timestamp;
        Broadcast(broadcast, -1);
      } else if (packet.type == epyks::PacketType::FRIEND_REQUEST) {
        epyks::FriendRequest req;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (req.Deserialize(bytes)) {
          if (req.target_username == username)
            return;

          bool targetOnline = false;
          SOCKET targetSock = INVALID_SOCKET;
          {
            std::lock_guard<std::mutex> lock(clientsMutex);
            for (auto &c : clients) {
              if (c.username == req.target_username) {
                targetOnline = true;
                targetSock = c.socket;
                break;
              }
            }
          }

          if (db) {
            db->CreateFriendRequest(username, req.target_username);
            if (targetOnline) {
              epyks::Packet notify;
              notify.type = epyks::PacketType::FRIEND_REQUEST;
              notify.data = username + " wants to add you as friend";
              SendTo(targetSock, notify);
            }
            if (gui)
              gui->AddLog("[" + username + "] sent friend request to [" +
                          req.target_username + "]");
          }
        }
      } else if (packet.type == epyks::PacketType::FRIEND_RESPONSE) {
        epyks::FriendResponse resp;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (resp.Deserialize(bytes) && db) {
          if (resp.accepted) {
            db->AcceptFriendRequest(username, resp.target_username);
            epyks::Packet notify;
            notify.type = epyks::PacketType::FRIEND_RESPONSE;
            notify.data = "You are now friends with " + resp.target_username;
            SendTo(sock, notify);

            {
              std::lock_guard<std::mutex> lock(clientsMutex);
              for (auto &c : clients) {
                if (c.username == resp.target_username) {
                  epyks::Packet notify2;
                  notify2.type = epyks::PacketType::FRIEND_RESPONSE;
                  notify2.data = "You are now friends with " + username;
                  SendTo(c.socket, notify2);
                  break;
                }
              }
            }

            if (gui)
              gui->AddLog("[" + username + "] and [" + resp.target_username +
                          "] are now friends");
          }
        }
      } else if (packet.type == epyks::PacketType::UNFRIEND) {
        epyks::Unfriend req;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (req.Deserialize(bytes) && db) {
          db->RemoveFriend(username, req.target_username);
          
          epyks::Packet notify;
          notify.type = epyks::PacketType::UNFRIEND;
          notify.data = username; // For the target
          
          epyks::Packet selfNotify;
          selfNotify.type = epyks::PacketType::UNFRIEND;
          selfNotify.data = req.target_username; // For the sender
          SendTo(sock, selfNotify);

          std::lock_guard<std::mutex> lock(clientsMutex);
          for (auto &c : clients) {
            if (c.username == req.target_username) {
              SendTo(c.socket, notify);
              break;
            }
          }
          if (gui)
            gui->AddLog("[" + username + "] unfriended [" +
                        req.target_username + "]");
        }
      } else if (packet.type == epyks::PacketType::PRIVATE_MESSAGE) {
        epyks::PrivateMessage pm;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (pm.Deserialize(bytes) && db) {
          if (db->AreFriends(username, pm.target_username)) {
            // Save to DB and get ID
            uint64_t msgId = db->SavePrivateMessage(username, pm.target_username, pm.content,
                                                   packet.timestamp, pm.reply_to_id);
            
            epyks::PrivateMessage forward;
            forward.target_username = pm.target_username; // From sender's perspective, still target
            forward.content = pm.content;
            forward.message_id = msgId;
            forward.reply_to_id = pm.reply_to_id;
            forward.sender_username = username; // So receiver knows who sent it
            
            auto forwardBytes = forward.Serialize();
            epyks::Packet fwdPkt;
            fwdPkt.type = epyks::PacketType::PRIVATE_MESSAGE;
            fwdPkt.data = std::string(forwardBytes.begin(), forwardBytes.end());
            fwdPkt.timestamp = packet.timestamp;

            std::lock_guard<std::mutex> lock(clientsMutex);
            for (auto &c : clients) {
              if (c.username == pm.target_username || c.username == username) {
                SendTo(c.socket, fwdPkt);
              }
            }

            if (gui)
              gui->AddLog("[DM][" + username + " -> " + pm.target_username +
                          "]: " + pm.content);
          } else {
            epyks::Packet reject;
            reject.type = epyks::PacketType::PRIVATE_MESSAGE;
            reject.data =
                "System: You are not friends with " + pm.target_username;
            SendTo(sock, reject);
          }
        }
      } else if (packet.type == epyks::PacketType::FRIEND_LIST) {
        if (db) {
          auto friends = db->GetFriends(username);
          epyks::FriendList list;
          list.usernames = friends;
          epyks::Packet response;
          response.type = epyks::PacketType::FRIEND_LIST;
          auto listBytes = list.Serialize();
          response.data = std::string(listBytes.begin(), listBytes.end());
          SendTo(sock, response);
        }
      } else if (packet.type == epyks::PacketType::CREATE_SERVER) {
        epyks::CreateServer req;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (req.Deserialize(bytes) && db) {
          if (db->CreateServer(req.server_name, username, req.password)) {
            int serverId = db->GetServerByName(req.server_name);

            epyks::JoinServer joinResp;
            joinResp.server_id = serverId;
            auto joinBytes = joinResp.Serialize();
            epyks::Packet joinNotify;
            joinNotify.type = epyks::PacketType::JOIN_SERVER;
            joinNotify.data = std::string(joinBytes.begin(), joinBytes.end());
            SendTo(sock, joinNotify);

            epyks::Packet notify;
            notify.type = epyks::PacketType::CREATE_SERVER;
            notify.data =
                "Server '" + req.server_name + "' created successfully";
            SendTo(sock, notify);
            if (gui)
              gui->AddLog("[" + username + "] created server [" +
                          req.server_name + "]");
          } else {
            if (gui)
              gui->AddLog("[DEBUG] CreateServer failed - name: " +
                          req.server_name + " owner: " + username);
            epyks::Packet notify;
            notify.type = epyks::PacketType::CREATE_SERVER;
            notify.data = "Failed to create server '" + req.server_name + "'";
            SendTo(sock, notify);
          }
        }
      } else if (packet.type == epyks::PacketType::JOIN_SERVER) {
        epyks::JoinServer req;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (req.Deserialize(bytes) && db) {
          if (db->JoinServer(username, req.server_id, req.password)) {
            epyks::JoinServer resp;
            resp.server_id = req.server_id;
            auto respBytes = resp.Serialize();

            epyks::Packet notify;
            notify.type = epyks::PacketType::JOIN_SERVER;
            notify.data = std::string(respBytes.begin(), respBytes.end());
            SendTo(sock, notify);
            if (gui)
              gui->AddLog("[" + username + "] joined the server");
          } else {
            epyks::Packet notify;
            notify.type = epyks::PacketType::JOIN_SERVER;
            notify.data = "Failed to join server";
            SendTo(sock, notify);
          }
        }
      } else if (packet.type == epyks::PacketType::LEAVE_SERVER) {
        epyks::LeaveServer req;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (req.Deserialize(bytes) && db) {
          bool isOwner = db->IsServerOwner(username, req.server_id);
          if (db->LeaveServer(username, req.server_id)) {
            if (isOwner) {
              auto members = db->GetServerMembers(req.server_id);
              db->DeleteServer(req.server_id);
              if (gui)
                gui->AddLog("Server [" + std::to_string(req.server_id) +
                            "] deleted because owner left");

              std::lock_guard<std::mutex> lock(clientsMutex);
              for (const auto &member : members) {
                for (auto &c : clients) {
                  if (c.username == member.first) {
                    epyks::Packet notify;
                    notify.type = epyks::PacketType::LEAVE_SERVER;
                    notify.data = std::to_string(req.server_id);
                    SendTo(c.socket, notify);
                    break;
                  }
                }
              }
            } else {
              auto members = db->GetServerMembers(req.server_id);
              if (members.empty()) {
                db->DeleteServer(req.server_id);
                if (gui)
                  gui->AddLog("Server [" + std::to_string(req.server_id) +
                              "] deleted because it became empty");
              }
            }
            epyks::Packet notify;
            notify.type = epyks::PacketType::LEAVE_SERVER;
            notify.data = std::to_string(req.server_id);
            SendTo(sock, notify);
            if (gui)
              gui->AddLog("[" + username + "] left the server");
          } else {
            epyks::Packet notify;
            notify.type = epyks::PacketType::LEAVE_SERVER;
            notify.data = "Failed to leave server";
            SendTo(sock, notify);
          }
        }
      } else if (packet.type == epyks::PacketType::SERVER_MESSAGE) {
        epyks::ServerMessage req;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (req.Deserialize(bytes) && db) {
          if (db->IsMuted(req.server_id, username)) {
            epyks::Packet notify;
            notify.type = epyks::PacketType::SERVER_MESSAGE;
            notify.data = "System: You are muted in this server.";
            SendTo(sock, notify);
            continue;
          }

          auto members = db->GetServerMembers(req.server_id);

          time_t now = time(nullptr);
          struct tm tm_info;
#ifdef _WIN32
          localtime_s(&tm_info, &now);
#else
          localtime_r(&now, &tm_info);
#endif
          char timeStr[16];
          strftime(timeStr, sizeof(timeStr), "%H:%M", &tm_info);

          epyks::ServerMessage forward;
          forward.server_id = req.server_id;
          forward.channel_id = req.channel_id;
          forward.username = username;
          forward.content = req.content;
          forward.reply_to_id = req.reply_to_id;
          
          if (db) {
            forward.message_id = db->SaveServerMessage(req.server_id, req.channel_id, username,
                                  forward.content, packet.timestamp, req.reply_to_id);
          }
          auto forwardBytes = forward.Serialize();

          std::lock_guard<std::mutex> lock(clientsMutex);
          for (const auto &member : members) {
            for (auto &c : clients) {
              if (c.username == member.first) {
                epyks::Packet notify;
                notify.type = epyks::PacketType::SERVER_MESSAGE;
                notify.data =
                    std::string(forwardBytes.begin(), forwardBytes.end());
                SendTo(c.socket, notify);
                break;
              }
            }
          }
        }
      } else if (packet.type == epyks::PacketType::EDIT_MESSAGE) {
        epyks::EditMessage req;
        auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (req.Deserialize(bytes) && db) {
          if (db->EditMessage(req.message_id, req.new_content)) {
            // Broadcast the edit to everyone in the server (if it's a server message)
            if (req.server_id != -1) {
              auto members = db->GetServerMembers(req.server_id);
              std::lock_guard<std::mutex> lock(clientsMutex);
              for (const auto &member : members) {
                for (auto &c : clients) {
                  if (c.username == member.first) {
                    epyks::Packet notify;
                    notify.type = epyks::PacketType::EDIT_MESSAGE;
                    notify.data = std::string(bytes.begin(), bytes.end());
                    SendTo(c.socket, notify);
                    break;
                  }
                }
              }
            } else {
                // If it's a private message, broadcast to both participants
                auto participants = db->GetMessageParticipants(req.message_id);
                std::lock_guard<std::mutex> lock(clientsMutex);
                for (auto &c : clients) {
                  if (c.username == participants.first || c.username == participants.second) {
                    epyks::Packet notify;
                    notify.type = epyks::PacketType::EDIT_MESSAGE;
                    notify.data = std::string(bytes.begin(), bytes.end());
                    SendTo(c.socket, notify);
                  }
                }
            }
          }
        }
      } else if (packet.type == epyks::PacketType::DELETE_MESSAGE) {
        epyks::DeleteMessage req;
        auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (req.Deserialize(bytes) && db) {
          if (db->DeleteMessage(req.message_id)) {
            if (req.server_id != -1) {
              auto members = db->GetServerMembers(req.server_id);
              std::lock_guard<std::mutex> lock(clientsMutex);
              for (const auto &member : members) {
                for (auto &c : clients) {
                  if (c.username == member.first) {
                    epyks::Packet notify;
                    notify.type = epyks::PacketType::DELETE_MESSAGE;
                    notify.data = std::string(bytes.begin(), bytes.end());
                    SendTo(c.socket, notify);
                    break;
                  }
                }
              }
            } else {
                // If it's a private message, broadcast to both participants
                auto participants = db->GetMessageParticipants(req.message_id);
                std::lock_guard<std::mutex> lock(clientsMutex);
                for (auto &c : clients) {
                  if (c.username == participants.first || c.username == participants.second) {
                    epyks::Packet notify;
                    notify.type = epyks::PacketType::DELETE_MESSAGE;
                    notify.data = std::string(bytes.begin(), bytes.end());
                    SendTo(c.socket, notify);
                  }
                }
            }
          }
        }
      } else if (packet.type == epyks::PacketType::DELETE_ACCOUNT) {
        epyks::DeleteAccount req;
        auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (req.Deserialize(bytes) && db) {
          if (db->ValidateLoginSecure(username, req.password)) {
            if (db->DeleteAccount(username)) {
              if (gui)
                gui->AddLog("[Auth] Account hard-deleted: " + username);
              
              // Disconnect the user
              std::lock_guard<std::mutex> lock(clientsMutex);
              for (auto it = clients.begin(); it != clients.end();) {
                if (it->username == username) {
                  closesocket(it->socket);
                  it = clients.erase(it);
                } else {
                  ++it;
                }
              }
            }
          }
        }
      } else if (packet.type == epyks::PacketType::LIST_SERVERS) {
        if (db) {
          auto list = db->GetAllServers();
          std::string result;
          for (auto &c : list) {
            result += std::to_string(std::get<0>(c)) + ":" + std::get<1>(c) +
                      ":" + (std::get<2>(c) ? "1" : "0") + ",";
          }
          epyks::Packet notify;
          notify.type = epyks::PacketType::LIST_SERVERS;
          notify.data = result;
          SendTo(sock, notify);
        }
      } else if (packet.type == epyks::PacketType::MY_SERVERS) {
        if (db) {
          auto servers = db->GetUserServers(username);
          std::string payload;
          for (auto &s : servers) {
            payload += std::to_string(std::get<0>(s)) + ":" + std::get<1>(s) + ":" + std::get<2>(s) + ",";
          }
          epyks::Packet notify;
          notify.type = epyks::PacketType::MY_SERVERS;
          notify.data = payload;
          SendTo(sock, notify);
        }
      } else if (packet.type == epyks::PacketType::MY_DMS) {
        if (db) {
          auto contacts = db->GetDMContacts(username);
          std::string payload;
          for (auto &c : contacts) payload += c + ",";
          epyks::Packet notify;
          notify.type = epyks::PacketType::MY_DMS;
          notify.data = payload;
          SendTo(sock, notify);
        }
      } else if (packet.type == epyks::PacketType::PROFILE_UPDATE) {
        epyks::ProfileUpdate req;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (req.Deserialize(bytes) && db) {
          db->UpdateProfile(username, req.bio, req.pfp_url);
          epyks::UserProfile profile;
          profile.username = username;
          profile.bio = req.bio;
          profile.pfp_url = req.pfp_url;
          auto pBytes = profile.Serialize();
          epyks::Packet notify;
          notify.type = epyks::PacketType::PROFILE_DATA;
          notify.data = std::string(pBytes.begin(), pBytes.end());
          Broadcast(notify, -1);
        }
      } else if (packet.type == epyks::PacketType::PFP_UPLOAD) {
        epyks::PfpUpload req;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (req.Deserialize(bytes) && !username.empty()) {
          std::filesystem::create_directories("pfps");
          std::string filename = "pfps/" + username + ".png";
          std::ofstream file(filename, std::ios::binary);
          if (file.is_open()) {
            file.write((const char *)req.image_data.data(),
                       req.image_data.size());
            file.close();

            if (db) {
              auto info = db->GetProfile(username);
              db->UpdateProfile(username, info.bio,
                                "server://" + username + ".png");
              epyks::UserProfile profile;
              profile.username = username;
              profile.bio = info.bio;
              profile.pfp_url = "server://" + username + ".png";
              auto pBytes = profile.Serialize();
              epyks::Packet notify;
              notify.type = epyks::PacketType::PROFILE_DATA;
              notify.data = std::string(pBytes.begin(), pBytes.end());
              Broadcast(notify, -1);
            }
            if (gui)
              gui->AddLog("[" + username + "] uploaded a new PFP (" +
                          std::to_string(req.image_data.size()) + " bytes)");
          }
        }
      } else if (packet.type == epyks::PacketType::PFP_REQUEST) {
        epyks::PfpRequest req;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (req.Deserialize(bytes)) {
          std::string filename = "pfps/" + req.username + ".png";
          if (std::filesystem::exists(filename)) {
            std::ifstream file(filename, std::ios::binary);
            if (file.is_open()) {
              epyks::PfpResponse resp;
              resp.username = req.username;
              resp.image_data.assign(std::istreambuf_iterator<char>(file),
                                     std::istreambuf_iterator<char>());
              epyks::Packet pkg;
              pkg.type = epyks::PacketType::PFP_RESPONSE;
              auto rBytes = resp.Serialize();
              pkg.data = std::string(rBytes.begin(), rBytes.end());
              SendTo(sock, pkg);
            }
          }
        }
      } else if (packet.type == epyks::PacketType::MEDIA_UPLOAD) {
        epyks::MediaUpload req;
        auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (req.Deserialize(bytes) && !username.empty()) {
          std::filesystem::create_directories("media");
          // Generate a unique filename using a timestamp
          auto now = std::chrono::system_clock::now().time_since_epoch().count();
          std::string mediaKey = username + "_" + std::to_string(now) + ".png";
          std::string filename = "media/" + mediaKey;
          std::ofstream file(filename, std::ios::binary);
          if (file.is_open()) {
            file.write((const char*)req.media_data.data(), req.media_data.size());
            file.close();

            // Respond with the SAME packed format as MEDIA_REQUEST delivery:
            // [4b fnLen][4b dataLen][filename bytes][image bytes]
            // This is the format the client's MEDIA_RESPONSE handler already parses.
            // It will cache the file locally AND set pendingMediaUploadUrl correctly.
            uint32_t fnLen  = static_cast<uint32_t>(mediaKey.size());
            uint32_t dataLen = static_cast<uint32_t>(req.media_data.size());
            std::vector<uint8_t> outBuf(8);
            std::memcpy(outBuf.data(),     &fnLen,   4);
            std::memcpy(outBuf.data() + 4, &dataLen, 4);
            outBuf.insert(outBuf.end(), mediaKey.begin(), mediaKey.end());
            outBuf.insert(outBuf.end(), req.media_data.begin(), req.media_data.end());

            epyks::Packet pkg;
            pkg.type = epyks::PacketType::MEDIA_RESPONSE;
            pkg.data = std::string(outBuf.begin(), outBuf.end());
            SendTo(sock, pkg);

            if (gui)
              gui->AddLog("[" + username + "] uploaded media (" +
                          std::to_string(req.media_data.size()) + " bytes) -> " + mediaKey);
          }
        }
      } else if (packet.type == epyks::PacketType::MEDIA_REQUEST) {
        epyks::MediaRequest req;
        auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (req.Deserialize(bytes)) {
          std::string filepath = "media/" + req.filename;
          if (std::filesystem::exists(filepath)) {
            std::ifstream file(filepath, std::ios::binary);
            if (file.is_open()) {
              epyks::MediaUpload resp;
              resp.media_data.assign(std::istreambuf_iterator<char>(file),
                                     std::istreambuf_iterator<char>());
              epyks::Packet pkg;
              pkg.type = epyks::PacketType::MEDIA_RESPONSE;
              // Re-use MediaResponse to send back the file + filename for the client to cache
              epyks::MediaResponse mr;
              mr.url = req.filename; // key, so client knows what to cache
              // We'll send the raw bytes as a separate MEDIA_UPLOAD packet type reused for delivery
              // Actually pack filename + data together into a custom payload
              std::vector<uint8_t> outBuf;
              uint32_t fnLen = static_cast<uint32_t>(req.filename.size());
              uint32_t dataLen = static_cast<uint32_t>(resp.media_data.size());
              outBuf.resize(8);
              std::memcpy(outBuf.data(), &fnLen, 4);
              std::memcpy(outBuf.data() + 4, &dataLen, 4);
              outBuf.insert(outBuf.end(), req.filename.begin(), req.filename.end());
              outBuf.insert(outBuf.end(), resp.media_data.begin(), resp.media_data.end());
              pkg.data = std::string(outBuf.begin(), outBuf.end());
              SendTo(sock, pkg);
            }
          }
        }
      } else if (packet.type == epyks::PacketType::GET_PROFILE) {
        std::string target = packet.data;
        if (target.empty())
          target = username;
        if (db) {
          auto info = db->GetProfile(target);
          epyks::UserProfile profile;
          profile.username = info.username;
          profile.bio = info.bio;
          profile.pfp_url = info.pfp_url;
          auto pBytes = profile.Serialize();
          epyks::Packet notify;
          notify.type = epyks::PacketType::PROFILE_DATA;
          notify.data = std::string(pBytes.begin(), pBytes.end());
          SendTo(sock, notify);
        }
      } else if (packet.type == epyks::PacketType::MEMBER_LIST_REQUEST) {
        epyks::MemberListRequest req;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (req.Deserialize(bytes) && db) {
          auto members = db->GetServerMembersDetailed(req.server_id);
          epyks::MemberListResponse res;
          res.server_id = req.server_id;

          std::lock_guard<std::mutex> lock(clientsMutex);
          for (auto &m : members) {
            epyks::MemberInfo mi;
            mi.username = m.username;
            mi.bio = m.bio;
            mi.pfp_url = m.pfp_url;
            mi.role = m.role;
            mi.is_muted = m.is_muted;
            mi.voice_channel_id = -1;
            mi.is_talking = false;

            for (auto &c : clients) {
              if (c.username == m.username) {
                if (c.currentVoiceServerId == req.server_id) {
                  mi.voice_channel_id = c.currentVoiceChannelId;
                  mi.is_talking = (GetTickCount64() - c.lastVoicePacket) < 500;
                }
                break;
              }
            }
            res.members.push_back(mi);
          }
          auto resBytes = res.Serialize();
          epyks::Packet notify;
          notify.type = epyks::PacketType::MEMBER_LIST_RESPONSE;
          notify.data = std::string(resBytes.begin(), resBytes.end());
          SendTo(sock, notify);
        }
      } else if (packet.type == epyks::PacketType::RENAME_SERVER) {
        try {
          size_t sep = packet.data.find('|');
          if (sep != std::string::npos && db) {
            int serverId = std::stoi(packet.data.substr(0, sep));
            std::string newName = packet.data.substr(sep + 1);
            if (gui)
              gui->AddLog("[DEBUG] User [" + username +
                          "] requesting rename for server [" +
                          std::to_string(serverId) + "] to [" + newName + "]");
            if (db->IsServerOwner(username, serverId)) {
              if (db->UpdateServerName(serverId, newName)) {
                epyks::Packet notify;
                notify.type = epyks::PacketType::RENAME_SERVER;
                notify.data = "Server renamed successfully";
                SendTo(sock, notify);
                if (gui)
                  gui->AddLog("[SUCCESS] Server [" + std::to_string(serverId) +
                              "] renamed to [" + newName + "]");
              } else {
                if (gui)
                  gui->AddLog(
                      "[ERROR] Database failed to update server name for [" +
                      std::to_string(serverId) + "]");
              }
            } else {
              epyks::Packet notify;
              notify.type = epyks::PacketType::RENAME_SERVER;
              notify.data = "Permission denied. Must be owner.";
              SendTo(sock, notify);
              if (gui)
                gui->AddLog("[DENIED] User [" + username +
                            "] is NOT owner of server [" +
                            std::to_string(serverId) + "]");
            }
          }
        } catch (...) {
          if (gui)
            gui->AddLog("[Error] Failed to process server rename");
        }
      } else if (packet.type == epyks::PacketType::CREATE_CHANNEL) {
        epyks::CreateChannel req;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (req.Deserialize(bytes) && db) {
          if (gui)
            gui->AddLog("[DEBUG] User [" + username + "] creating channel [" +
                        req.channel_name + "] in server [" +
                        std::to_string(req.server_id) + "]");
          if (db->IsServerOwner(username, req.server_id)) {
            int channelId = db->CreateChannel(req.server_id, req.channel_name,
                                              req.type, req.category);
            if (channelId != -1) {
              epyks::Packet notify;
              notify.type = epyks::PacketType::CREATE_CHANNEL;
              notify.data = "Channel created successfully";
              SendTo(sock, notify);
              if (gui)
                gui->AddLog("[SUCCESS] Channel [" + req.channel_name +
                            "] created in server [" +
                            std::to_string(req.server_id) + "]");
            } else {
              if (gui)
                gui->AddLog("[ERROR] Database failed to create channel [" +
                            req.channel_name + "]");
            }
          } else {
            epyks::Packet notify;
            notify.type = epyks::PacketType::CREATE_CHANNEL;
            notify.data = "Permission denied. Must be owner.";
            SendTo(sock, notify);
            if (gui)
              gui->AddLog("[DENIED] User [" + username +
                          "] is NOT owner of server [" +
                          std::to_string(req.server_id) + "]");
          }
        }
      } else if (packet.type == epyks::PacketType::DELETE_CHANNEL) {
        epyks::DeleteChannel req;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (req.Deserialize(bytes) && db) {
          if (db->IsServerOwner(username, req.server_id)) {
            db->DeleteChannel(req.channel_id);
            epyks::Packet notify;
            notify.type = epyks::PacketType::DELETE_CHANNEL;
            notify.data = "Channel deleted successfully";
            SendTo(sock, notify);
            if (gui)
              gui->AddLog("[" + username + "] deleted a channel");
          } else {
            epyks::Packet notify;
            notify.type = epyks::PacketType::DELETE_CHANNEL;
            notify.data = "Permission denied. Must be owner.";
            SendTo(sock, notify);
          }
        }
      } else if (packet.type == epyks::PacketType::EDIT_CHANNEL) {
        epyks::EditChannel req;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (req.Deserialize(bytes) && db) {
          if (db->IsServerOwner(username, req.server_id)) {
            db->EditChannel(req.channel_id, req.name, req.type, req.category);
            epyks::Packet notify;
            notify.type = epyks::PacketType::EDIT_CHANNEL;
            notify.data = "Channel edited successfully";
            SendTo(sock, notify);
            if (gui)
              gui->AddLog("[" + username + "] edited a channel");
          } else {
            epyks::Packet notify;
            notify.type = epyks::PacketType::EDIT_CHANNEL;
            notify.data = "Permission denied. Must be owner.";
            SendTo(sock, notify);
          }
        }
      } else if (packet.type == epyks::PacketType::CHANNEL_LIST) {
        epyks::ChannelList req;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (req.Deserialize(bytes) && db) {
          auto channels = db->GetChannels(req.server_id);
          std::string result;
          for (auto &c : channels) {
            result += std::to_string(std::get<0>(c)) + ":" + std::get<1>(c) +
                      ":" + std::to_string(std::get<2>(c)) + ":" +
                      std::get<3>(c) + ",";
          }
          epyks::ChannelList resp;
          resp.server_id = req.server_id;
          resp.data = result;
          auto respBytes = resp.Serialize();
          epyks::Packet notify;
          notify.type = epyks::PacketType::CHANNEL_LIST;
          notify.data = std::string(respBytes.begin(), respBytes.end());
          SendTo(sock, notify);
        }
      } else if (packet.type == epyks::PacketType::KICK_USER) {
        epyks::KickUser req;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (req.Deserialize(bytes) && db) {
          if (db->IsServerOwner(username, req.server_id)) {
            db->KickUser(req.server_id, req.target_username);
            epyks::Packet notify;
            notify.type = epyks::PacketType::KICK_USER;
            notify.data = std::to_string(req.server_id) + "|" + req.target_username + " was kicked.";
            
            std::lock_guard<std::mutex> lock(clientsMutex);
            for (auto &c : clients) {
                if (c.username == req.target_username) {
                    SendTo(c.socket, notify); // Notify the victim
                } else {
                    // Maybe notify others in the server? For now just notify the sender too.
                    if (c.socket == sock) SendTo(sock, notify); 
                }
            }
            if (gui)
              gui->AddLog("[" + username + "] kicked " + req.target_username);
          } else {
            epyks::Packet notify;
            notify.type = epyks::PacketType::KICK_USER;
            notify.data = "Permission denied. Must be owner.";
            SendTo(sock, notify);
          }
        }
      } else if (packet.type == epyks::PacketType::MUTE_USER) {
        epyks::MuteUser req;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (req.Deserialize(bytes) && db) {
          if (db->IsServerOwner(username, req.server_id)) {
            db->MuteUser(req.server_id, req.target_username, req.is_muted);
            epyks::Packet notify;
            notify.type = epyks::PacketType::MUTE_USER;
            notify.data = req.target_username +
                          (req.is_muted ? " was muted." : " was unmuted.");
            SendTo(sock, notify);
            if (gui)
              gui->AddLog("[" + username + "] muted/unmuted " +
                          req.target_username);
          } else {
            epyks::Packet notify;
            notify.type = epyks::PacketType::MUTE_USER;
            notify.data = "Permission denied. Must be owner.";
            SendTo(sock, notify);
          }
        }
      } else if (packet.type == epyks::PacketType::JOIN_VOICE) {
        epyks::JoinVoice req;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (req.Deserialize(bytes) && db) {
          auto servers = db->GetUserServers(username);
          bool isMember = false;
          for (auto &s : servers) {
            if (std::get<0>(s) == req.server_id) {
              isMember = true;
              break;
            }
          }
          if (isMember) {
            std::lock_guard<std::mutex> lock(clientsMutex);
            for (auto &c : clients) {
              if (c.socket == sock) {
                c.currentVoiceServerId = req.server_id;
                c.currentVoiceChannelId = req.channel_id;
                c.hasUdpAddr = false;
                break;
              }
            }
            if (gui)
              gui->AddLog("[" + username + "] joined voice channel " +
                          std::to_string(req.channel_id));
          }
        }
      } else if (packet.type == epyks::PacketType::LEAVE_VOICE) {
        epyks::LeaveVoice req;
        auto bytes =
            std::vector<uint8_t>(packet.data.begin(), packet.data.end());
        if (req.Deserialize(bytes)) {
          std::lock_guard<std::mutex> lock(clientsMutex);
          for (auto &c : clients) {
            if (c.socket == sock) {
              c.currentVoiceServerId = -1;
              c.currentVoiceChannelId = -1;
              c.hasUdpAddr = false;
              break;
            }
          }
          if (gui)
            gui->AddLog("[" + username + "] left voice channel");
        }
      }
    }

    if (!username.empty()) {
      if (gui)
        gui->AddLog("[" + username + "] left the chat");
      LogToFile("[" + username + "] left the chat");
      epyks::Packet leaveMsg;
      leaveMsg.type = epyks::PacketType::JOIN_LEAVE;
      leaveMsg.data = username + " left the chat";
      Broadcast(leaveMsg, sock);
    }
    RemoveClient(sock);
  }

  void RemoveClient(SOCKET sock) {
    std::lock_guard<std::mutex> lock(clientsMutex);
    clients.erase(
        std::remove_if(clients.begin(), clients.end(),
                       [sock](const Client &c) { return c.socket == sock; }),
        clients.end());
    closesocket(sock);
  }

  bool ReceivePacket(SOCKET sock, epyks::Packet &packet) {
    uint32_t len = 0;
    if (recv(sock, (char *)&len, 4, MSG_WAITALL) != 4)
      return false;
    if (len > 10000000) // 10MB max
      return false;

    std::vector<uint8_t> buffer(len);
    int received = 0;
    while (received < (int)len) {
      int r = recv(sock, (char *)buffer.data() + received, len - received, 0);
      if (r <= 0)
        return false;
      received += r;
    }

    return packet.Deserialize(buffer);
  }

  void SendTo(SOCKET sock, const epyks::Packet &packet) {
    auto data = packet.Serialize();
    uint32_t len = (uint32_t)data.size();
    send(sock, (char *)&len, 4, 0);
    send(sock, (char *)data.data(), len, 0);
  }

  void Broadcast(const epyks::Packet &packet, SOCKET exclude) {
    auto data = packet.Serialize();
    uint32_t len = (uint32_t)data.size();

    std::lock_guard<std::mutex> lock(clientsMutex);
    for (auto &client : clients) {
      if (!client.hasUsername || !client.authenticated)
        continue;
      if (exclude != -1 && client.socket == exclude)
        continue;
      send(client.socket, (char *)&len, 4, 0);
      send(client.socket, (char *)data.data(), len, 0);
    }
  }
};

int main(int argc, char *argv[]) {
  int port = 9001;
  for (int i = 1; i < argc; i++) {
    if (std::string(argv[i]) == "--port" && i + 1 < argc)
      port = std::stoi(argv[++i]);
  }

  printf("\n--- Epyks Server (Headless Console) ---\n");

  Database database;
  std::filesystem::create_directories("epyks_data");
  std::string dbPath = "epyks_data/epyks_chat.db";

  if (!database.Open(dbPath)) {
    printf("[Error] Failed to open database at %s\n", dbPath.c_str());
    return 1;
  }
  printf("[System] Database OK\n");
  LogToFile("Database initialized successfully.");

  ChatServer server;
  server.SetDatabase(&database);

  if (server.Start(port)) {
    printf("[System] Server listening on port %d\n", port);
    printf("[System] Press Ctrl+C to stop.\n");
    while (true) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }

  return 0;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    AllocConsole();
    FILE* fDummy;
    freopen_s(&fDummy, "CONIN$", "r", stdin);
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);
    return main(__argc, __argv);
}
#endif