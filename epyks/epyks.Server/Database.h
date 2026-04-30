#pragma once
#include <sqlite3.h>
#include <string>
#include <vector>
#include <cstdint>
#include <tuple>
#include <mutex>

struct ChatMessage {
    uint64_t id = 0;
    std::string username;
    std::string message;
    uint64_t timestamp = 0;
    uint64_t reply_to_id = 0;
};

struct UserProfileInfo {
    std::string username;
    std::string bio;
    std::string pfp_url;
    int role = 0;
    bool is_muted = false;
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
    bool UpdateProfile(const std::string& username, const std::string& bio, const std::string& pfp_url);
    UserProfileInfo GetProfile(const std::string& username);
    bool DeleteAccount(const std::string& username);

    // sessions
    bool SaveSessionToken(const std::string& username, const std::string& token);
    bool ValidateSessionToken(const std::string& username, const std::string& token);
    void ClearSessionToken(const std::string& username);

    // friends
    bool CreateFriendRequest(const std::string& fromUser, const std::string& toUser);
    std::vector<std::string> GetPendingFriendRequests(const std::string& username);
    bool AcceptFriendRequest(const std::string& user1, const std::string& user2);
    bool RemoveFriend(const std::string& user1, const std::string& user2);
    std::vector<std::string> GetFriends(const std::string& username);
    bool AreFriends(const std::string& user1, const std::string& user2);

    // private messages
    uint64_t SavePrivateMessage(const std::string& from, const std::string& to, const std::string& message, uint64_t timestamp, uint64_t reply_to_id = 0);
    std::vector<ChatMessage> GetPrivateMessages(const std::string& user1, const std::string& user2, int limit = 100);
    std::vector<std::string> GetDMContacts(const std::string& username);

    // servers
    bool CreateServer(const std::string& name, const std::string& owner, const std::string& password = "");
    bool JoinServer(const std::string& username, int serverId, const std::string& password = "");
    bool UpdateServerName(int serverId, const std::string& newName);
    uint64_t SaveServerMessage(int serverId, int channelId, const std::string& username, const std::string& message, uint64_t timestamp, uint64_t reply_to_id = 0);
    std::vector<ChatMessage> GetServerMessages(int serverId, int channelId, int limit = 100);
    
    // global messages
    bool EditMessage(uint64_t messageId, const std::string& newContent);
    bool DeleteMessage(uint64_t messageId);
    std::vector<std::pair<std::string, int>> GetServerMembers(int serverId);
    std::pair<std::string, std::string> GetMessageParticipants(uint64_t messageId);
    std::vector<UserProfileInfo> GetServerMembersDetailed(int serverId);
    bool LeaveServer(const std::string& username, int serverId);
    int GetServerByName(const std::string& serverName);
    std::vector<std::tuple<int, std::string, bool>> GetAllServers();
    std::vector<std::tuple<int, std::string, std::string>> GetUserServers(const std::string& username);
    bool DeleteServer(int serverId);
    bool IsServerOwner(const std::string& username, int serverId);

    //channels
    int CreateChannel(int serverId, const std::string& name, int type, const std::string& category = "Uncategorized");
    bool DeleteChannel(int channelId);
    bool EditChannel(int channelId, const std::string& name, int type, const std::string& category);

    std::vector<std::tuple<int, std::string, int, std::string>> GetChannels(int serverId);

    //server moderation
    bool KickUser(int serverId, const std::string& target_username);
    bool MuteUser(int serverId, const std::string& target_username, bool mute);
    bool IsMuted(int serverId, const std::string& username);


private:
    bool Execute(const std::string& sql);
    std::recursive_mutex dbMutex;
};