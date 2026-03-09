#include "Database.h"
#include <iostream>
#include <random>
#include <sstream>
#include <iomanip>

bool Database::Open(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        db = nullptr;
        return false;
    }

    Execute("PRAGMA journal_mode=WAL;");


    Execute(
        "CREATE TABLE IF NOT EXISTS accounts ("
        "username TEXT PRIMARY KEY,"
        "password TEXT,"
        "salt TEXT);"
    );

    Execute(
        "CREATE TABLE IF NOT EXISTS sessions ("
        "username TEXT PRIMARY KEY,"
        "token TEXT);"
    );

    Execute(
        "CREATE TABLE IF NOT EXISTS friends ("
        "user1 TEXT,"
        "user2 TEXT,"
        "UNIQUE(user1, user2));"
    );

    Execute(
        "CREATE TABLE IF NOT EXISTS chat_messages ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT,"
        "message TEXT,"
        "timestamp INTEGER);"
    );

    Execute(
        "CREATE TABLE IF NOT EXISTS private_messages ("
        "sender TEXT,"
        "receiver TEXT,"
        "message TEXT,"
        "timestamp INTEGER);"
    );

    Execute(
        "CREATE TABLE IF NOT EXISTS groups ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT,"
        "owner TEXT);"
	);

    Execute(
        "CREATE TABLE IF NOT EXISTS group_members ("
        "group_id INTEGER,"
        "username TEXT,"
        "UNIQUE(group_id, username));"
	);

    return true;
}

void Database::Close() {
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
}

bool Database::Execute(const std::string& sql) {
    char* err = nullptr;
    bool ok = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) == SQLITE_OK;
    if (err) {
        std::cerr << "SQL Error: " << err << std::endl;
        sqlite3_free(err);
    }
    return ok;
}


//chat shit
void Database::ClearChatHistory() {
    Execute("DELETE FROM chat_messages;");
}

void Database::SaveMessage(const std::string& username, const std::string& message, uint64_t timestamp) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "INSERT INTO chat_messages (username, message, timestamp) VALUES(?,?,?)",
        -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, message.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, timestamp);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::vector<ChatMessage> Database::GetRecentMessages(int limit) {
    std::vector<ChatMessage> result;

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "SELECT username, message, timestamp FROM chat_messages "
        "ORDER BY id DESC LIMIT ?",
        -1, &stmt, nullptr);

    sqlite3_bind_int(stmt, 1, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        result.push_back({
            (const char*)sqlite3_column_text(stmt, 0),
            (const char*)sqlite3_column_text(stmt, 1),
            (uint64_t)sqlite3_column_int64(stmt, 2)
            });
    }

    sqlite3_finalize(stmt);
    std::reverse(result.begin(), result.end());
    return result;
}


//security and accounts
std::string Database::GenerateSalt() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 255);

    std::ostringstream ss;
    for (int i = 0; i < 16; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << dist(gen);
    }
    return ss.str();
}

std::string Database::HashPassword(const std::string& password, const std::string& salt) {

    std::string combined = salt + password;
    std::hash<std::string> hasher;
    size_t hash = hasher(combined);


    for (int i = 0; i < 10000; i++) {
        hash = hasher(std::to_string(hash) + salt + std::to_string(i));
    }


    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << hash;
    return ss.str();
}

bool Database::AccountExists(const std::string& username) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "SELECT 1 FROM accounts WHERE username=?",
        -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    bool exists = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return exists;
}

bool Database::CreateAccountSecure(const std::string& username, const std::string& password) {
    std::string salt = GenerateSalt();
    std::string hash = HashPassword(password, salt);

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "INSERT INTO accounts (username, password, salt) VALUES(?,?,?)",
        -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, salt.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::ValidateLoginSecure(const std::string& username, const std::string& password) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "SELECT password, salt FROM accounts WHERE username=?",
        -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    bool valid = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string storedHash = (const char*)sqlite3_column_text(stmt, 0);
        std::string salt = (const char*)sqlite3_column_text(stmt, 1);
        std::string computedHash = HashPassword(password, salt);
        valid = (storedHash == computedHash);
    }

    sqlite3_finalize(stmt);
    return valid;
}



bool Database::SaveSessionToken(const std::string& username, const std::string& token) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "REPLACE INTO sessions VALUES(?,?)",
        -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, token.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::ValidateSessionToken(const std::string& username, const std::string& token) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "SELECT token FROM sessions WHERE username=?",
        -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    bool valid = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        valid = token == (const char*)sqlite3_column_text(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return valid;
}

void Database::ClearSessionToken(const std::string& username) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "DELETE FROM sessions WHERE username=?",
        -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}


//friends
bool Database::CreateFriendRequest(const std::string& from, const std::string& to) {

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO friends (user1, user2) VALUES(?,?)",
        -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1, from.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, to.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::AcceptFriendRequest(const std::string& u1, const std::string& u2) {

    bool ok1 = CreateFriendRequest(u1, u2);
    bool ok2 = CreateFriendRequest(u2, u1);
    return ok1 && ok2;
}

bool Database::RemoveFriend(const std::string& u1, const std::string& u2) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "DELETE FROM friends WHERE (user1=? AND user2=?) OR (user1=? AND user2=?)",
        -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1, u1.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, u2.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, u2.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, u1.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<std::string> Database::GetFriends(const std::string& user) {
    std::vector<std::string> out;
    sqlite3_stmt* stmt;

    sqlite3_prepare_v2(db,
        "SELECT user2 FROM friends WHERE user1=?",
        -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1, user.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.emplace_back((const char*)sqlite3_column_text(stmt, 0));
    }

    sqlite3_finalize(stmt);
    return out;
}

bool Database::AreFriends(const std::string& u1, const std::string& u2) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "SELECT 1 FROM friends WHERE user1=? AND user2=?",
        -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1, u1.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, u2.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return ok;
}


//groups
bool Database::CreateGroup(const std::string& name, const std::string& owner) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
         "INSERT INTO groups(group_name, created_by) VALUES(?, ?)",
         -1, & stmt, nullptr);

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, owner.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::JoinGroup(const std::string& username, int groupId) {
	sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
         "INSERT INTO group_members(group_id, username) VALUES(?, ?)",
		-1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, groupId);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
	return ok;
}

std::vector<std::string> Database::GetGroupMembers(int groupId) {
	sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
         "SELECT username FROM group_members WHERE group_id=?",
        -1, &stmt, nullptr);
	sqlite3_bind_int(stmt, 1, groupId);
    std::vector<std::string> members;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        members.emplace_back((const char*)sqlite3_column_text(stmt, 0));
    }
	sqlite3_finalize(stmt);
	return members;
}

bool Database::LeaveGroup(const std::string& username, int groupId) {
	sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
         "DELETE FROM group_members WHERE group_id=? AND username=?",
		-1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, groupId);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
	sqlite3_finalize(stmt);
	return ok;
}

int Database::GetGroupByName(const std::string& groupName) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "SELECT id FROM groups WHERE name=?",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, groupName.c_str(), -1, SQLITE_TRANSIENT);
    int groupId = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        groupId = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return groupId;
}
std::vector<std::pair<int, std::string>> Database::GetAllGroups() {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "SELECT id, name FROM groups",
        -1, &stmt, nullptr);
    std::vector<std::pair<int, std::string>> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.emplace_back(sqlite3_column_int(stmt, 0), (const char*)sqlite3_column_text(stmt, 1));
    }
    sqlite3_finalize(stmt);
    return results;
}

//private messages

void Database::SavePrivateMessage(const std::string& from, const std::string& to,
    const std::string& msg, uint64_t ts) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "INSERT INTO private_messages VALUES(?,?,?,?)",
        -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1, from.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, to.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, msg.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, ts);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::vector<ChatMessage> Database::GetPrivateMessages(const std::string& u1, const std::string& u2, int limit) {
    std::vector<ChatMessage> out;

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "SELECT sender, message, timestamp FROM private_messages "
        "WHERE (sender=? AND receiver=?) OR (sender=? AND receiver=?) "
        "ORDER BY timestamp DESC LIMIT ?",
        -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1, u1.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, u2.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, u2.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, u1.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.push_back({
            (const char*)sqlite3_column_text(stmt, 0),
            (const char*)sqlite3_column_text(stmt, 1),
            (uint64_t)sqlite3_column_int64(stmt, 2)
            });
    }

    sqlite3_finalize(stmt);
    std::reverse(out.begin(), out.end());
    return out;
}