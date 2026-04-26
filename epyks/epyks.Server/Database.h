#pragma once
#include <sqlite3.h>
#include <string>
#include <vector>
#include <cstdint>
#include <tuple>

struct ChatMessage {
    std::string username;
    std::string message;
    uint64_t timestamp;
};

class Database {
    sqlite3* db = nullptr;

public:
    bool Open(const std::string& path);
    void Close();
    std::string GenerateSalt();
    std::string HashPassword(const std::string& password, const std::string& salt);

    // chat
    void ClearChatHistory();
    void SaveMessage(const std::string& username, const std::string& message, uint64_t timestamp);
    std::vector<ChatMessage> GetRecentMessages(int limit = 100);

    // accounts
    bool CreateAccountSecure(const std::string& username, const std::string& password);
    bool ValidateLoginSecure(const std::string& username, const std::string& password);
    bool AccountExists(const std::string& username);

    // sessions
    bool SaveSessionToken(const std::string& username, const std::string& token);
    bool ValidateSessionToken(const std::string& username, const std::string& token);
    void ClearSessionToken(const std::string& username);

    // friends
    bool CreateFriendRequest(const std::string& fromUser, const std::string& toUser);
    bool AcceptFriendRequest(const std::string& user1, const std::string& user2);
    bool RemoveFriend(const std::string& user1, const std::string& user2);
    std::vector<std::string> GetFriends(const std::string& username);
    bool AreFriends(const std::string& user1, const std::string& user2);

    // private messages
    void SavePrivateMessage(const std::string& from, const std::string& to, const std::string& message, uint64_t timestamp);
    std::vector<ChatMessage> GetPrivateMessages(const std::string& user1, const std::string& user2, int limit = 100);

    //servers
    bool CreateServer(const std::string& name, const std::string& owner, const std::string& password = "");
    bool JoinServer(const std::string& username, int serverId, const std::string& password = "");
    std::vector<std::pair<std::string, int>> GetServerMembers(int serverId);
    bool LeaveServer(const std::string& username, int serverId);
    int GetServerByName(const std::string& serverName);
    std::vector<std::pair<int, std::string>> GetAllServers();
    std::vector<std::pair<int, std::string>> GetUserServers(const std::string& username);
    bool DeleteServer(int serverId);
    bool IsServerOwner(const std::string& username, int serverId);

    //channels
    int CreateChannel(int serverId, const std::string& name, int type);
    bool DeleteChannel(int channelId);
    std::vector<std::tuple<int, std::string, int>> GetChannels(int serverId);

    //server moderation
    bool KickUser(int serverId, const std::string& target_username);
    bool MuteUser(int serverId, const std::string& target_username, bool mute);
    bool IsMuted(int serverId, const std::string& username);


private:
    bool Execute(const std::string& sql);
};