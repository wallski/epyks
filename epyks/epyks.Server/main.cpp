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

        threads.emplace_back(&ChatServer::AcceptLoop, this);
        return true;
    }

    void Stop() {
        accepting = false;
        running = false;

        closesocket(listenSocket);

        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            for (auto& c : clients) {
                closesocket(c.socket);
            }
        }

        for (auto& t : threads) {
            if (t.joinable()) t.join();
        }

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
                clients.push_back({ clientSocket, "", false, false });
            }

            threads.emplace_back(&ChatServer::HandleClient, this, clientSocket);
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
            else if (packet.type == epyks::PacketType::CREATE_GROUP) {
                epyks::CreateGroup req;
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (req.Deserialize(bytes) && db) {
                    if (db->CreateGroup(req.group_name, username)) {
                        epyks::Packet notify;
                        notify.type = epyks::PacketType::CREATE_GROUP;
                        notify.data = "Group '" + req.group_name + "' created successfully";
                        SendTo(sock, notify);
                        if (gui) gui->AddLog("[" + username + "] created group [" + req.group_name + "]");
                    }
                    else {
                        epyks::Packet notify;
                        notify.type = epyks::PacketType::CREATE_GROUP;
                        notify.data = "Failed to create group '" + req.group_name + "'";
                        SendTo(sock, notify);
                    }
                }
			}
            else if (packet.type == epyks::PacketType::JOIN_GROUP) {
                epyks::JoinGroup req;
				auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (req.Deserialize(bytes) && db) {
                    if (db->JoinGroup(username, req.group_id)) {
                        epyks::Packet notify;
                        notify.type = epyks::PacketType::JOIN_GROUP;
                        notify.data = "Group joined successfully";
                        SendTo(sock, notify);
                        if (gui) gui->AddLog("[" + username + "] joined the group");
                    }
                    else {
                        epyks::Packet notify;
                        notify.type = epyks::PacketType::JOIN_GROUP;
                        notify.data = "Failed to join group";
                        SendTo(sock, notify);
                    }
                }
            }
            else if (packet.type == epyks::PacketType::LEAVE_GROUP) {
                epyks::LeaveGroup req;
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (req.Deserialize(bytes) && db) {
                    if (db->LeaveGroup(username, req.group_id)) {
                        epyks::Packet notify;
                        notify.type = epyks::PacketType::LEAVE_GROUP;
                        notify.data = "Left Group successfully";
                        SendTo(sock, notify);
                        if (gui) gui->AddLog("[" + username + "] left the group");
                    }
                    else {
                        epyks::Packet notify;
                        notify.type = epyks::PacketType::LEAVE_GROUP;
                        notify.data = "Failed to leave group";
                        SendTo(sock, notify);
                    }
                }
            }
            else if (packet.type == epyks::PacketType::GROUP_MESSAGE) {
                epyks::GroupMessage req;
                auto bytes = std::vector<uint8_t>(packet.data.begin(), packet.data.end());
                if (req.Deserialize(bytes) && db) {
                    auto members = db->GetGroupMembers(req.group_id);

                    std::lock_guard<std::mutex> lock(clientsMutex);
                    for (const auto& member : members) {
                        for (auto& c : clients) {
                            if (c.username == member) {
                                epyks::Packet notify;
                                notify.type = epyks::PacketType::GROUP_MESSAGE;
                                notify.data = "[Group " + std::to_string(req.group_id) + "] " + username + ": " + req.content;
                                SendTo(c.socket, notify);
                                break;
                            }
                        }
                    }
                }
            }
            else if (packet.type == epyks::PacketType::LIST_GROUPS) {
                auto list = db->GetAllGroups();
                std::string result;
                for (auto& c : list) {
                    result += std::to_string(c.first) + ":" + c.second + ",";
                }
                epyks::Packet notify;
                notify.type = epyks::PacketType::LIST_GROUPS;
                notify.data = result;
                SendTo(sock, notify);

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

int main(int, char**) {
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