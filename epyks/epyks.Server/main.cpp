#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include "Protocol/Packet.h"
#include "ServerGUI.h"
#include "Database.h"
#include <filesystem>
#pragma comment(lib, "ws2_32.lib")


struct Client {
    SOCKET socket;
    std::string username;
    bool hasUsername = false;
    bool authenticated = false;
    
    // Voice tracking
    int currentVoiceServerId = -1;
    int currentVoiceChannelId = -1;
    sockaddr_in udpAddr = {};
    bool hasUdpAddr = false;
};

class ChatServer {
    SOCKET listenSocket = INVALID_SOCKET;
    std::atomic<bool> running{ false };
    std::atomic<bool> accepting{ false };
    std::vector<std::thread> threads;
    std::vector<Client> clients;
    std::mutex clientsMutex;
    uint64_t nextUserId = 0;
    ServerGUI* gui = nullptr;
    Database* db = nullptr;
    int port = 9001;

    SOCKET udpSocket = INVALID_SOCKET;
    std::thread udpThread;

public:
    void SetGUI(ServerGUI* g) { gui = g; }
    void SetDatabase(Database* d) { db = d; }

    bool Start(int p) {
        port = p;

        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            if (gui) gui->AddLog("[Error] WSAStartup failed");
            return false;
        }

        listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSocket == INVALID_SOCKET) {
            if (gui) gui->AddLog("[Error] Socket creation failed");
            WSACleanup();
            return false;
        }

        int opt = 1;
        setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(listenSocket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            if (gui) gui->AddLog("[Error] Failed to bind to port " + std::to_string(port));
            closesocket(listenSocket);
            WSACleanup();
            return false;
        }

        if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
            if (gui) gui->AddLog("[Error] Listen failed");
            closesocket(listenSocket);
            WSACleanup();
            return false;
        }

        running = true;
        accepting = true;

        udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (udpSocket != INVALID_SOCKET) {
            int optUdp = 1;
            setsockopt(udpSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&optUdp, sizeof(optUdp));
            if (bind(udpSocket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
                if (gui) gui->AddLog("[Error] Failed to bind UDP port " + std::to_string(port));
                closesocket(udpSocket);
                udpSocket = INVALID_SOCKET;
            } else {
                if (gui) gui->AddLog("UDP Server listening on port " + std::to_string(port));
                udpThread = std::thread(&ChatServer::UdpLoop, this);
            }
        }

        threads.emplace_back(&ChatServer::AcceptLoop, this);
        return true;
    }

    void Stop() {
        accepting = false;
        running = false;

        closesocket(listenSocket);
        if (udpSocket != INVALID_SOCKET) closesocket(udpSocket);

        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            for (auto& c : clients) {
                closesocket(c.socket);
            }
        }

        for (auto& t : threads) {
            if (t.joinable()) t.join();
        }
        if (udpThread.joinable()) udpThread.join();

        WSACleanup();
    }

    void AcceptLoop() {
        while (accepting) {
            sockaddr_in clientAddr;
            int addrLen = sizeof(clientAddr);
            SOCKET clientSocket = accept(listenSocket, (sockaddr*)&clientAddr, &addrLen);

            if (clientSocket == INVALID_SOCKET) {
                if (accepting && gui) gui->AddLog("[Error] Accept failed");
                continue;
            }

            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddr.sin_addr, ip, INET_ADDRSTRLEN);
            if (gui) gui->AddLog("Connection from " + std::string(ip));

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
            int bytes = recvfrom(udpSocket, buffer, sizeof(buffer), 0, (sockaddr*)&senderAddr, &senderLen);
            if (bytes > 0) {
                std::vector<uint8_t> data(buffer, buffer + bytes);
                epyks::VoiceData pkt;
                if (pkt.Deserialize(data)) {
                    std::lock_guard<std::mutex> lock(clientsMutex);
                    
                    // Verify sender and update UDP endpoint
                    bool senderVerified = false;
                    for (auto& c : clients) {
                        if (c.authenticated && c.username == pkt.username && 
                            c.currentVoiceServerId == pkt.server_id && c.currentVoiceChannelId == pkt.channel_id) {
                            c.udpAddr = senderAddr;
                            c.hasUdpAddr = true;
                            senderVerified = true;
                            break;
                        }
                    }

                    // Route to participants
                    if (senderVerified) {
                        for (auto& c : clients) {
                            if (c.authenticated && c.hasUdpAddr && c.username != pkt.username && 
                                c.currentVoiceServerId == pkt.server_id && c.currentVoiceChannelId == pkt.channel_id) {
                                sendto(udpSocket, buffer, bytes, 0, (sockaddr*)&c.udpAddr, sizeof(c.udpAddr));
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
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (req.Deserialize(bytes) && db) {
                    epyks::RegisterResponse resp;
                    if (db->AccountExists(req.username)) {
                        resp.success = false;
                        resp.error = "Username already exists";
                    }
                    else if (req.username.length() < 3) {
                        resp.success = false;
                        resp.error = "Username must be at least 3 characters";
                    }
                    else if (req.password.length() < 4) {
                        resp.success = false;
                        resp.error = "Password must be at least 4 characters";
                    }
                    else {
                        db->CreateAccountSecure(req.username, req.password);
                        resp.success = true;
                        if (gui) gui->AddLog("[Auth] Account created: " + req.username);
                    }
                    auto respBytes = resp.Serialize();
                    epyks::Packet response;
                    response.type = epyks::PacketType::REGISTER_RESPONSE;
                    response.data = std::string(respBytes.begin(), respBytes.end());
                    SendTo(sock, response);
                }
            }
            else if (packet.type == epyks::PacketType::LOGIN) {
                epyks::LoginRequest req;
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (req.Deserialize(bytes) && db) {
                    epyks::LoginResponse resp;
                    if (db->ValidateLoginSecure(req.username, req.password)) {
                        authenticated = true;
                        username = req.username;
                        resp.success = true;
 
                        sessionToken = db->GenerateSalt() + db->GenerateSalt();
                        db->SaveSessionToken(username, sessionToken);
                        resp.session_token = sessionToken;
                        if (gui) gui->AddLog("[Auth] Login success: " + req.username);
                    }
                    else {
                        resp.success = false;
                        resp.error = "Invalid username or password";
                        if (gui) gui->AddLog("[Auth] Login failed: " + req.username);
                    }
                    auto respBytes = resp.Serialize();
                    epyks::Packet response;
                    response.type = epyks::PacketType::LOGIN_RESPONSE;
                    response.data = std::string(respBytes.begin(), respBytes.end());
                    SendTo(sock, response);
                }
            }
            else if (packet.type == epyks::PacketType::TOKEN_LOGIN) {
                epyks::TokenLoginRequest req;
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (req.Deserialize(bytes) && db) {
                    epyks::LoginResponse resp;
                    if (db->ValidateSessionToken(req.username, req.token)) {
                        authenticated = true;
                        username = req.username;
                        resp.success = true;
   
                        sessionToken = db->GenerateSalt() + db->GenerateSalt();
                        db->SaveSessionToken(username, sessionToken);
                        resp.session_token = sessionToken;
                        if (gui) gui->AddLog("[Auth] Token login success: " + req.username);
                    }
                    else {
                        resp.success = false;
                        resp.error = "Session expired";
                        if (gui) gui->AddLog("[Auth] Token login failed: " + req.username);
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
            for (auto& c : clients) {
                if (c.socket == sock) {
                    c.username = username;
                    c.hasUsername = true;
                    c.authenticated = true;
                    break;
                }
            }
        }

        if (gui) gui->AddLog("[" + username + "] logged in");


        if (db) {
            auto messages = db->GetRecentMessages(100);
            for (auto& msg : messages) {
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
            if (!ReceivePacket(sock, packet)) break;

            if (packet.type == epyks::PacketType::CHAT_MESSAGE) {
                if (gui) gui->AddLog("[" + username + "]: " + packet.data);
                if (db) db->SaveMessage(username, packet.data, packet.timestamp);

                epyks::Packet broadcast;
                broadcast.type = epyks::PacketType::CHAT_MESSAGE;
                broadcast.data = "[" + username + "]: " + packet.data;
                broadcast.timestamp = packet.timestamp;
                Broadcast(broadcast, -1);
            }
            else if (packet.type == epyks::PacketType::FRIEND_REQUEST) {
                epyks::FriendRequest req;
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (req.Deserialize(bytes)) {
                    if (req.target_username == username) return;

                    bool targetOnline = false;
                    SOCKET targetSock = INVALID_SOCKET;
                    {
                        std::lock_guard<std::mutex> lock(clientsMutex);
                        for (auto& c : clients) {
                            if (c.username == req.target_username) {
                                targetOnline = true;
                                targetSock = c.socket;
                                break;
                            }
                        }
                    }
                    if (targetOnline && db) {
                        db->CreateFriendRequest(username, req.target_username);
                        epyks::Packet notify;
                        notify.type = epyks::PacketType::FRIEND_REQUEST;
                        notify.data = username + " wants to add you as friend";
                        SendTo(targetSock, notify);
                        if (gui) gui->AddLog("[" + username + "] sent friend request to [" + req.target_username + "]");
                    }
                }
            }
            else if (packet.type == epyks::PacketType::FRIEND_RESPONSE) {
                epyks::FriendResponse resp;
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (resp.Deserialize(bytes) && db) {
                    if (resp.accepted) {
                        db->AcceptFriendRequest(username, resp.target_username);

 
                        epyks::Packet notify;
                        notify.type = epyks::PacketType::FRIEND_RESPONSE;
                        notify.data = "You are now friends with " + resp.target_username;


                        SendTo(sock, notify);


                        {
                            std::lock_guard<std::mutex> lock(clientsMutex);
                            for (auto& c : clients) {
                                if (c.username == resp.target_username) {
                                    epyks::Packet notify2;
                                    notify2.type = epyks::PacketType::FRIEND_RESPONSE;
                                    notify2.data = "You are now friends with " + username;
                                    SendTo(c.socket, notify2);
                                    break;
                                }
                            }
                        }

                        if (gui) gui->AddLog("[" + username + "] and [" + resp.target_username + "] are now friends");
                    }
                }
            }
            else if (packet.type == epyks::PacketType::UNFRIEND) {
                epyks::Unfriend req;
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (req.Deserialize(bytes) && db) {
                    db->RemoveFriend(username, req.target_username);
                    // notify the other person
                    std::lock_guard<std::mutex> lock(clientsMutex);
                    for (auto& c : clients) {
                        if (c.username == req.target_username) {
                            epyks::Packet notify;
                            notify.type = epyks::PacketType::UNFRIEND;
                            notify.data = username;
                            SendTo(c.socket, notify);
                            break;
                        }
                    }
                    if (gui) gui->AddLog("[" + username + "] unfriended [" + req.target_username + "]");
                }
            }
            else if (packet.type == epyks::PacketType::PRIVATE_MESSAGE) {
                epyks::PrivateMessage pm;
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (pm.Deserialize(bytes) && db) {
                    if (db->AreFriends(username, pm.target_username)) {
                        SOCKET targetSock = INVALID_SOCKET;
                        {
                            std::lock_guard<std::mutex> lock(clientsMutex);
                            for (auto& c : clients) {
                                if (c.username == pm.target_username) {
                                    targetSock = c.socket;
                                    break;
                                }
                            }
                        }
                        if (targetSock != INVALID_SOCKET) {
                            epyks::Packet fwd;
                            fwd.type = epyks::PacketType::PRIVATE_MESSAGE;
                            fwd.data = "[DM from " + username + "]: " + pm.content;
                            fwd.timestamp = packet.timestamp;
                            SendTo(targetSock, fwd);
                            epyks::Packet confirm;
                            confirm.type = epyks::PacketType::PRIVATE_MESSAGE;
                            confirm.data = "[DM to " + pm.target_username + "]: " + pm.content;
                            confirm.timestamp = packet.timestamp;
                            SendTo(sock, confirm);
                            db->SavePrivateMessage(username, pm.target_username, pm.content, packet.timestamp);
                            if (gui) gui->AddLog("[DM][" + username + " -> " + pm.target_username + "]: " + pm.content);
                        }
                    }
                    else {
                        epyks::Packet reject;
                        reject.type = epyks::PacketType::PRIVATE_MESSAGE;
                        reject.data = "System: You are not friends with " + pm.target_username;
                        SendTo(sock, reject);
                    }
                }
            }
            else if (packet.type == epyks::PacketType::FRIEND_LIST) {
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
            }
            else if (packet.type == epyks::PacketType::CREATE_SERVER) {
                epyks::CreateServer req;
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
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
                        notify.data = "Server '" + req.server_name + "' created successfully";
                        SendTo(sock, notify);
                        if (gui) gui->AddLog("[" + username + "] created server [" + req.server_name + "]");
                    }
                    else {
                        if (gui) gui->AddLog("[DEBUG] CreateServer failed - name: " + req.server_name + " owner: " + username);
                        epyks::Packet notify;
                        notify.type = epyks::PacketType::CREATE_SERVER;
                        notify.data = "Failed to create server '" + req.server_name + "'";
                        SendTo(sock, notify);
                    }
                }
 			}
            else if (packet.type == epyks::PacketType::JOIN_SERVER) {
                epyks::JoinServer req;
 				auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (req.Deserialize(bytes) && db) {
                    if (db->JoinServer(username, req.server_id, req.password)) {
                        epyks::JoinServer resp;
                        resp.server_id = req.server_id;
                        auto respBytes = resp.Serialize();

                        epyks::Packet notify;
                        notify.type = epyks::PacketType::JOIN_SERVER;
                        notify.data = std::string(respBytes.begin(), respBytes.end());
                        SendTo(sock, notify);
                        if (gui) gui->AddLog("[" + username + "] joined the server");
                    }
                    else {
                        epyks::Packet notify;
                        notify.type = epyks::PacketType::JOIN_SERVER;
                        notify.data = "Failed to join server";
                        SendTo(sock, notify);
                    }
                }
            }
            else if (packet.type == epyks::PacketType::LEAVE_SERVER) {
                epyks::LeaveServer req;
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (req.Deserialize(bytes) && db) {
                    bool isOwner = db->IsServerOwner(username, req.server_id);
                    if (db->LeaveServer(username, req.server_id)) {
                        if (isOwner) {
                            auto members = db->GetServerMembers(req.server_id);
                            db->DeleteServer(req.server_id);
                            if (gui) gui->AddLog("Server [" + std::to_string(req.server_id) + "] deleted because owner left");
                            
                            std::lock_guard<std::mutex> lock(clientsMutex);
                            for (const auto& member : members) {
                                for (auto& c : clients) {
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
                                if (gui) gui->AddLog("Server [" + std::to_string(req.server_id) + "] deleted because it became empty");
                            }
                        }
                        epyks::Packet notify;
                        notify.type = epyks::PacketType::LEAVE_SERVER;
                        notify.data = std::to_string(req.server_id);
                        SendTo(sock, notify);
                        if (gui) gui->AddLog("[" + username + "] left the server");
                    }
                    else {
                        epyks::Packet notify;
                        notify.type = epyks::PacketType::LEAVE_SERVER;
                        notify.data = "Failed to leave server";
                        SendTo(sock, notify);
                    }
                }
            }
            else if (packet.type == epyks::PacketType::SERVER_MESSAGE) {
                epyks::ServerMessage req;
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (req.Deserialize(bytes) && db) {
                    if (db->IsMuted(req.server_id, username)) {
                        epyks::Packet notify;
                        notify.type = epyks::PacketType::SERVER_MESSAGE;
                        notify.data = "System: You are muted in this server.";
                        SendTo(sock, notify);
                        continue;
                    }
                    
                    auto members = db->GetServerMembers(req.server_id);

                    epyks::ServerMessage forward;
                    forward.server_id = req.server_id;
                    forward.channel_id = req.channel_id;
                    forward.content = username + ": " + req.content;
                    auto forwardBytes = forward.Serialize();

                    std::lock_guard<std::mutex> lock(clientsMutex);
                    for (const auto& member : members) {
                        for (auto& c : clients) {
                            if (c.username == member.first) {
                                epyks::Packet notify;
                                notify.type = epyks::PacketType::SERVER_MESSAGE;
                                notify.data = std::string(forwardBytes.begin(), forwardBytes.end());
                                SendTo(c.socket, notify);
                                break;
                            }
                        }
                    }
                }
            }
            else if (packet.type == epyks::PacketType::LIST_SERVERS) {
                if (db) {
                    auto list = db->GetAllServers();
                    std::string result;
                    for (auto& c : list) {
                        result += std::to_string(c.first) + ":" + c.second + ",";
                    }
                    epyks::Packet notify;
                    notify.type = epyks::PacketType::LIST_SERVERS;
                    notify.data = result;
                    SendTo(sock, notify);
                }
            }
            else if (packet.type == epyks::PacketType::MY_SERVERS) {
                if (db) {
                    auto servers = db->GetUserServers(username);
                    std::string payload;
                    for (auto& s : servers) {
                        payload += std::to_string(s.first) + ":" + s.second + ",";
                    }
                    epyks::Packet notify;
                    notify.type = epyks::PacketType::MY_SERVERS;
                    notify.data = payload;
                    SendTo(sock, notify);
                }
            }
            else if (packet.type == epyks::PacketType::PROFILE_UPDATE) {
                epyks::ProfileUpdate req;
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
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
                    SendTo(sock, notify);
                }
            }
            else if (packet.type == epyks::PacketType::GET_PROFILE) {
                std::string target = packet.data;
                if (target.empty()) target = username;
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
            }
            else if (packet.type == epyks::PacketType::MEMBER_LIST_REQUEST) {
                epyks::MemberListRequest req;
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (req.Deserialize(bytes) && db) {
                    auto members = db->GetServerMembersDetailed(req.server_id);
                    epyks::MemberListResponse res;
                    res.server_id = req.server_id;
                    for (auto& m : members) {
                        epyks::MemberInfo mi;
                        mi.username = m.username;
                        mi.bio = m.bio;
                        mi.pfp_url = m.pfp_url;
                        mi.role = m.role;
                        mi.is_muted = m.is_muted;
                        res.members.push_back(mi);
                    }
                    auto resBytes = res.Serialize();
                    epyks::Packet notify;
                    notify.type = epyks::PacketType::MEMBER_LIST_RESPONSE;
                    notify.data = std::string(resBytes.begin(), resBytes.end());
                    SendTo(sock, notify);
                }
            }
            else if (packet.type == epyks::PacketType::CREATE_CHANNEL) {
                epyks::CreateChannel req;
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (req.Deserialize(bytes) && db) {
                    if (db->IsServerOwner(username, req.server_id)) {
                        int channelId = db->CreateChannel(req.server_id, req.channel_name, req.type);
                        if (channelId != -1) {
                            epyks::Packet notify;
                            notify.type = epyks::PacketType::CREATE_CHANNEL;
                            notify.data = "Channel created successfully";
                            SendTo(sock, notify);
                            if (gui) gui->AddLog("[" + username + "] created channel [" + req.channel_name + "]");
                        }
                    } else {
                        epyks::Packet notify;
                        notify.type = epyks::PacketType::CREATE_CHANNEL;
                        notify.data = "Permission denied. Must be owner.";
                        SendTo(sock, notify);
                    }
                }
            }
            else if (packet.type == epyks::PacketType::DELETE_CHANNEL) {
                epyks::DeleteChannel req;
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (req.Deserialize(bytes) && db) {
                    if (db->IsServerOwner(username, req.server_id)) {
                        db->DeleteChannel(req.channel_id);
                        epyks::Packet notify;
                        notify.type = epyks::PacketType::DELETE_CHANNEL;
                        notify.data = "Channel deleted successfully";
                        SendTo(sock, notify);
                        if (gui) gui->AddLog("[" + username + "] deleted a channel");
                    } else {
                        epyks::Packet notify;
                        notify.type = epyks::PacketType::DELETE_CHANNEL;
                        notify.data = "Permission denied. Must be owner.";
                        SendTo(sock, notify);
                    }
                }
            }
            else if (packet.type == epyks::PacketType::CHANNEL_LIST) {
                epyks::ChannelList req;
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (req.Deserialize(bytes) && db) {
                    auto channels = db->GetChannels(req.server_id);
                    std::string payload;
                    for (auto& c : channels) {
                        payload += std::to_string(std::get<0>(c)) + ":" + std::get<1>(c) + ":" + std::to_string(std::get<2>(c)) + ",";
                    }
                    epyks::ChannelList resp;
                    resp.server_id = req.server_id;
                    resp.data = payload;
                    auto respBytes = resp.Serialize();
                    epyks::Packet notify;
                    notify.type = epyks::PacketType::CHANNEL_LIST;
                    notify.data = std::string(respBytes.begin(), respBytes.end());
                    SendTo(sock, notify);
                }
            }
            else if (packet.type == epyks::PacketType::KICK_USER) {
                epyks::KickUser req;
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (req.Deserialize(bytes) && db) {
                    if (db->IsServerOwner(username, req.server_id)) {
                        db->KickUser(req.server_id, req.target_username);
                        epyks::Packet notify;
                        notify.type = epyks::PacketType::KICK_USER;
                        notify.data = req.target_username + " was kicked.";
                        SendTo(sock, notify);
                        if (gui) gui->AddLog("[" + username + "] kicked " + req.target_username);
                    } else {
                        epyks::Packet notify;
                        notify.type = epyks::PacketType::KICK_USER;
                        notify.data = "Permission denied. Must be owner.";
                        SendTo(sock, notify);
                    }
                }
            }
            else if (packet.type == epyks::PacketType::MUTE_USER) {
                epyks::MuteUser req;
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (req.Deserialize(bytes) && db) {
                    if (db->IsServerOwner(username, req.server_id)) {
                        db->MuteUser(req.server_id, req.target_username, req.is_muted);
                        epyks::Packet notify;
                        notify.type = epyks::PacketType::MUTE_USER;
                        notify.data = req.target_username + (req.is_muted ? " was muted." : " was unmuted.");
                        SendTo(sock, notify);
                        if (gui) gui->AddLog("[" + username + "] muted/unmuted " + req.target_username);
                    } else {
                        epyks::Packet notify;
                        notify.type = epyks::PacketType::MUTE_USER;
                        notify.data = "Permission denied. Must be owner.";
                        SendTo(sock, notify);
                    }
                }
            }
            else if (packet.type == epyks::PacketType::JOIN_VOICE) {
                epyks::JoinVoice req;
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (req.Deserialize(bytes) && db) {
                    // Just verify they are in the server
                    auto servers = db->GetUserServers(username);
                    bool isMember = false;
                    for (auto& s : servers) {
                        if (s.first == req.server_id) {
                            isMember = true;
                            break;
                        }
                    }
                    if (isMember) {
                        std::lock_guard<std::mutex> lock(clientsMutex);
                        for (auto& c : clients) {
                            if (c.socket == sock) {
                                c.currentVoiceServerId = req.server_id;
                                c.currentVoiceChannelId = req.channel_id;
                                c.hasUdpAddr = false; // Need to receive a UDP packet to set this
                                break;
                            }
                        }
                        if (gui) gui->AddLog("[" + username + "] joined voice channel " + std::to_string(req.channel_id));
                    }
                }
            }
            else if (packet.type == epyks::PacketType::LEAVE_VOICE) {
                epyks::LeaveVoice req;
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (req.Deserialize(bytes)) {
                    std::lock_guard<std::mutex> lock(clientsMutex);
                    for (auto& c : clients) {
                        if (c.socket == sock) {
                            c.currentVoiceServerId = -1;
                            c.currentVoiceChannelId = -1;
                            c.hasUdpAddr = false;
                            break;
                        }
                    }
                    if (gui) gui->AddLog("[" + username + "] left voice channel");
                }
            }
        }


        if (!username.empty()) {
            if (gui) gui->AddLog("[" + username + "] left the chat");
            epyks::Packet leaveMsg;
            leaveMsg.type = epyks::PacketType::JOIN_LEAVE;
            leaveMsg.data = username + " left the chat";
            Broadcast(leaveMsg, sock);
        }
        RemoveClient(sock);
    }

    void RemoveClient(SOCKET sock) {
        std::lock_guard<std::mutex> lock(clientsMutex);
        clients.erase(std::remove_if(clients.begin(), clients.end(),
            [sock](const Client& c) { return c.socket == sock; }), clients.end());
        closesocket(sock);
    }

    bool ReceivePacket(SOCKET sock, epyks::Packet& packet) {
        uint32_t len = 0;
        if (recv(sock, (char*)&len, 4, MSG_WAITALL) != 4) return false;
        if (len > 1000000) return false;

        std::vector<uint8_t> buffer(len);
        int received = 0;
        while (received < (int)len) {
            int r = recv(sock, (char*)buffer.data() + received, len - received, 0);
            if (r <= 0) return false;
            received += r;
        }

        return packet.Deserialize(buffer);
    }

    void SendTo(SOCKET sock, const epyks::Packet& packet) {
        auto data = packet.Serialize();
        uint32_t len = (uint32_t)data.size();
        send(sock, (char*)&len, 4, 0);
        send(sock, (char*)data.data(), len, 0);
    }

    void Broadcast(const epyks::Packet& packet, SOCKET exclude) {
        auto data = packet.Serialize();
        uint32_t len = (uint32_t)data.size();

        std::lock_guard<std::mutex> lock(clientsMutex);
        for (auto& client : clients) {
            if (!client.hasUsername || !client.authenticated) continue;
            if (exclude != -1 && client.socket == exclude) continue;
            send(client.socket, (char*)&len, 4, 0);
            send(client.socket, (char*)data.data(), len, 0);
        }
    }
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    ServerGUI gui;
    if (!gui.Initialize()) return 1;

    Database database;


    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string exeDir = exePath;
    exeDir = exeDir.substr(0, exeDir.find_last_of("\\/"));

    std::string dataDir = exeDir + "\\epyks_data";
    std::string dbPath = dataDir + "\\epyks_chat.db";

    CreateDirectoryA(dataDir.c_str(), nullptr);

    if (!database.Open(dbPath)) {
        gui.AddLog("[Error] Database failed: " + dbPath);
    }
    else {
        gui.AddLog("[System] Database OK: " + dbPath);
        gui.SetDatabase(&database);

        auto messages = database.GetRecentMessages(1000);
        for (const auto& msg : messages) {
            gui.AddLog(msg.username + ": " + msg.message);
        }

        gui.AddLog("[System] Chat history loaded from database.");
        gui.scrollToBottom = true;
    }

    ChatServer server;
    server.SetGUI(&gui);
    server.SetDatabase(&database);

    while (gui.RunFrame()) {
        if (gui.ShouldStartServer()) {
            if (server.Start(gui.GetPort())) {
                gui.ServerStarted();
            }
            else {
                gui.ClearFlags();
                gui.AddLog("[Error] Failed to start server.");
            }
        }

        if (gui.ShouldStopServer()) {
            server.Stop();
            gui.ServerStopped();
        }
    }

    if (gui.IsServerRunning()) {
        server.Stop();
        gui.ServerStopped();
    }

    database.Close();
    gui.Shutdown();
    return 0;
}