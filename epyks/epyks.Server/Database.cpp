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
    Execute("PRAGMA synchronous=NORMAL;");

    // Drop old tables
    Execute("DROP TABLE IF EXISTS chat_groups;");
    Execute("DROP TABLE IF EXISTS group_members;");

    Execute(
        "CREATE TABLE IF NOT EXISTS accounts ("
        "username TEXT PRIMARY KEY,"
        "password TEXT,"
        "salt TEXT,"
        "bio TEXT DEFAULT '',"
        "pfp_url TEXT DEFAULT '');"
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
        "CREATE TABLE IF NOT EXISTS server_messages ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "server_id INTEGER,"
        "channel_id INTEGER,"
        "username TEXT,"
        "message TEXT,"
        "timestamp INTEGER);"
    );

    Execute(
        "CREATE TABLE IF NOT EXISTS servers ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT,"
        "password_hash TEXT,"
        "owner TEXT);"
    );

    Execute(
        "CREATE TABLE IF NOT EXISTS channels ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "server_id INTEGER,"
        "name TEXT,"
        "type INTEGER,"
        "category TEXT DEFAULT 'Uncategorized');"
    );

    Execute(
        "CREATE TABLE IF NOT EXISTS server_members ("
        "server_id INTEGER,"
        "username TEXT,"
        "role INTEGER,"
        "is_muted INTEGER,"
        "UNIQUE(server_id, username));"
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
    std::lock_guard<std::mutex> lock(dbMutex);
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
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<ChatMessage> result;

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "SELECT username, message, timestamp FROM chat_messages "
        "ORDER BY id DESC LIMIT ?",
        -1, &stmt, nullptr);

    sqlite3_bind_int(stmt, 1, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* user = (const char*)sqlite3_column_text(stmt, 0);
        const char* msg = (const char*)sqlite3_column_text(stmt, 1);
        result.push_back({
            user ? user : "",
            msg ? msg : "",
            (uint64_t)sqlite3_column_int64(stmt, 2)
            });
    }

    sqlite3_finalize(stmt);
    std::reverse(result.begin(), result.end());
    return result;
}

void Database::SaveServerMessage(int serverId, int channelId, const std::string& username, const std::string& message, uint64_t timestamp) {
    std::lock_guard<std::mutex> lock(dbMutex);
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "INSERT INTO server_messages(server_id, channel_id, username, message, timestamp) VALUES(?, ?, ?, ?, ?)",
        -1, &stmt, nullptr);

    sqlite3_bind_int(stmt, 1, serverId);
    sqlite3_bind_int(stmt, 2, channelId);
    sqlite3_bind_text(stmt, 3, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, message.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, timestamp);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::vector<ChatMessage> Database::GetServerMessages(int serverId, int channelId, int limit) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<ChatMessage> result;
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "SELECT username, message, timestamp FROM server_messages "
        "WHERE server_id=? AND channel_id=? "
        "ORDER BY id DESC LIMIT ?",
        -1, &stmt, nullptr);

    sqlite3_bind_int(stmt, 1, serverId);
    sqlite3_bind_int(stmt, 2, channelId);
    sqlite3_bind_int(stmt, 3, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* user = (const char*)sqlite3_column_text(stmt, 0);
        const char* msg = (const char*)sqlite3_column_text(stmt, 1);
        result.push_back({
            user ? user : "",
            msg ? msg : "",
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


//servers
bool Database::CreateServer(const std::string& name, const std::string& owner, const std::string& password) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
         "INSERT INTO servers(name, owner, password_hash) VALUES(?, ?, ?)",
         -1, & stmt, nullptr);

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, owner.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, password.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    
    if (ok) {
        int serverId = (int)sqlite3_last_insert_rowid(db);
        JoinServer(owner, serverId, password);
        Execute("UPDATE server_members SET role=1 WHERE server_id=" + std::to_string(serverId) + " AND username='" + owner + "'");
    }
    
    return ok;
}

bool Database::JoinServer(const std::string& username, int serverId, const std::string& password) {
	sqlite3_stmt* stmt;
    
    sqlite3_prepare_v2(db, "SELECT password_hash FROM servers WHERE id=?", -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, serverId);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string expectedPass = (const char*)sqlite3_column_text(stmt, 0);
        if (!expectedPass.empty() && expectedPass != password) {
            sqlite3_finalize(stmt);
            return false;
        }
    } else {
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_finalize(stmt);

    sqlite3_prepare_v2(db,
         "INSERT INTO server_members(server_id, username, role, is_muted) VALUES(?, ?, 0, 0)",
		-1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, serverId);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
	return ok;
}

std::vector<std::pair<std::string, int>> Database::GetServerMembers(int serverId) {
	sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
         "SELECT username, role FROM server_members WHERE server_id=?",
        -1, &stmt, nullptr);
	sqlite3_bind_int(stmt, 1, serverId);
    std::vector<std::pair<std::string, int>> members;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        members.emplace_back((const char*)sqlite3_column_text(stmt, 0), sqlite3_column_int(stmt, 1));
    }
	sqlite3_finalize(stmt);
	return members;
}

bool Database::LeaveServer(const std::string& username, int serverId) {
	sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
         "DELETE FROM server_members WHERE server_id=? AND username=?",
		-1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, serverId);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
	sqlite3_finalize(stmt);
	return ok;
}

int Database::GetServerByName(const std::string& serverName) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "SELECT id FROM servers WHERE name=?",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, serverName.c_str(), -1, SQLITE_TRANSIENT);
    int serverId = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        serverId = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return serverId;
}

std::vector<std::tuple<int, std::string, bool>> Database::GetAllServers() {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "SELECT id, name, password_hash FROM servers",
        -1, &stmt, nullptr);
    std::vector<std::tuple<int, std::string, bool>> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* pass = (const char*)sqlite3_column_text(stmt, 2);
        bool hasPass = (pass != nullptr && strlen(pass) > 0);
        results.emplace_back(sqlite3_column_int(stmt, 0), (const char*)sqlite3_column_text(stmt, 1), hasPass);
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<std::pair<int, std::string>> Database::GetUserServers(const std::string& username) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "SELECT servers.id, servers.name FROM servers "
        "INNER JOIN server_members ON servers.id = server_members.server_id "
        "WHERE server_members.username = ?",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    std::vector<std::pair<int, std::string>> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.emplace_back(sqlite3_column_int(stmt, 0), (const char*)sqlite3_column_text(stmt, 1));
    }
    sqlite3_finalize(stmt);
    return results;
}

bool Database::DeleteServer(int serverId) {
    Execute("DELETE FROM server_members WHERE server_id=" + std::to_string(serverId) + ";");
    Execute("DELETE FROM channels WHERE server_id=" + std::to_string(serverId) + ";");
    Execute("DELETE FROM servers WHERE id=" + std::to_string(serverId) + ";");
    return true;
}

bool Database::UpdateServerName(int serverId, const std::string& newName) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "UPDATE servers SET name=? WHERE id=?", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, newName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, serverId);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::IsServerOwner(const std::string& username, int serverId) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "SELECT role FROM server_members WHERE server_id=? AND username=?",
        -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, serverId);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    bool isOwner = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        isOwner = sqlite3_column_int(stmt, 0) == 1;
    }
    sqlite3_finalize(stmt);
    return isOwner;
}

//channels
int Database::CreateChannel(int serverId, const std::string& name, int type, const std::string& category) {
	sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
         "INSERT INTO channels(server_id, name, type, category) VALUES(?, ?, ?, ?)",
		-1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, serverId);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, type);
    sqlite3_bind_text(stmt, 4, category.c_str(), -1, SQLITE_TRANSIENT);
    
    int id = -1;
    if (sqlite3_step(stmt) == SQLITE_DONE) {
        id = (int)sqlite3_last_insert_rowid(db);
    }
	sqlite3_finalize(stmt);
	return id;
}

bool Database::DeleteChannel(int channelId) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
         "DELETE FROM channels WHERE id=?",
         -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, channelId);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<std::tuple<int, std::string, int, std::string>> Database::GetChannels(int serverId) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "SELECT id, name, type, category FROM channels WHERE server_id=?",
        -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, serverId);
    std::vector<std::tuple<int, std::string, int, std::string>> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.emplace_back(
            sqlite3_column_int(stmt, 0), 
            (const char*)sqlite3_column_text(stmt, 1), 
            sqlite3_column_int(stmt, 2),
            sqlite3_column_text(stmt, 3) ? (const char*)sqlite3_column_text(stmt, 3) : "Uncategorized"
        );
    }
    sqlite3_finalize(stmt);
    return results;
}

//server moderation
bool Database::KickUser(int serverId, const std::string& target_username) {
    return LeaveServer(target_username, serverId);
}

bool Database::MuteUser(int serverId, const std::string& target_username, bool mute) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
         "UPDATE server_members SET is_muted=? WHERE server_id=? AND username=?",
         -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, mute ? 1 : 0);
    sqlite3_bind_int(stmt, 2, serverId);
    sqlite3_bind_text(stmt, 3, target_username.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::IsMuted(int serverId, const std::string& username) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "SELECT is_muted FROM server_members WHERE server_id=? AND username=?",
        -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, serverId);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    bool muted = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        muted = sqlite3_column_int(stmt, 0) == 1;
    }
    sqlite3_finalize(stmt);
    return muted;
}

//private messages

void Database::SavePrivateMessage(const std::string& from, const std::string& to,
    const std::string& msg, uint64_t ts) {
    std::lock_guard<std::mutex> lock(dbMutex);
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
    std::lock_guard<std::mutex> lock(dbMutex);
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

bool Database::UpdateProfile(const std::string& username, const std::string& bio, const std::string& pfp_url) {
    std::lock_guard<std::mutex> lock(dbMutex);
    sqlite3_stmt* stmt;
    const char* sql = "UPDATE accounts SET bio = ?, pfp_url = ? WHERE username = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, bio.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, pfp_url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, username.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

UserProfileInfo Database::GetProfile(const std::string& username) {
    std::lock_guard<std::mutex> lock(dbMutex);
    UserProfileInfo info;
    info.username = username;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT bio, pfp_url FROM accounts WHERE username = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* bio = (const char*)sqlite3_column_text(stmt, 0);
            const char* pfp = (const char*)sqlite3_column_text(stmt, 1);
            if (bio) info.bio = bio;
            if (pfp) info.pfp_url = pfp;
        }
        sqlite3_finalize(stmt);
    }
    return info;
}

std::vector<UserProfileInfo> Database::GetServerMembersDetailed(int serverId) {
    std::lock_guard<std::mutex> lock(dbMutex);
    std::vector<UserProfileInfo> members;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT m.username, a.bio, a.pfp_url, m.role, m.is_muted "
                      "FROM server_members m "
                      "JOIN accounts a ON m.username = a.username "
                      "WHERE m.server_id = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, serverId);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            UserProfileInfo m;
            m.username = (const char*)sqlite3_column_text(stmt, 0);
            const char* bio = (const char*)sqlite3_column_text(stmt, 1);
            const char* pfp = (const char*)sqlite3_column_text(stmt, 2);
            if (bio) m.bio = bio;
            if (pfp) m.pfp_url = pfp;
            m.role = sqlite3_column_int(stmt, 3);
            m.is_muted = sqlite3_column_int(stmt, 4) != 0;
            members.push_back(m);
        }
        sqlite3_finalize(stmt);
    }
    return members;
}