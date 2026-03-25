#pragma once
#include <sqlite3.h>
#include <string>
#include <vector>
#include <cstdint>

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

    //groups
    bool CreateGroup(const std::string& name, const std::string& owner);
    bool JoinGroup(const std::string& username, int groupId);
    std::vector<std::string> GetGroupMembers(int groupId);
    bool LeaveGroup(const std::string& username, int groupId);
    int GetGroupByName(const std::string& groupName);
    std::vector<std::pair<int, std::string>> GetAllGroups();
    std::vector<std::pair<int, std::string>> GetUserGroups(const std::string& username);
    bool DeleteGroup(int groupId);


private:
    bool Execute(const std::string& sql);
};