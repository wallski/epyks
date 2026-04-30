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

    // Core Tables
    Execute("CREATE TABLE IF NOT EXISTS accounts ("
            "username TEXT PRIMARY KEY,"
            "password TEXT,"
            "salt TEXT,"
            "bio TEXT DEFAULT '',"
            "pfp_url TEXT DEFAULT '');");

    Execute("CREATE TABLE IF NOT EXISTS sessions ("
            "username TEXT PRIMARY KEY,"
            "token TEXT);");

    Execute("CREATE TABLE IF NOT EXISTS friends ("
            "user1 TEXT,"
            "user2 TEXT,"
            "UNIQUE(user1, user2));");

    Execute("CREATE TABLE IF NOT EXISTS chat_messages ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "username TEXT,"
            "message TEXT,"
            "timestamp INTEGER,"
            "reply_to_id INTEGER DEFAULT 0);");

    Execute("CREATE TABLE IF NOT EXISTS private_messages ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "sender TEXT,"
            "receiver TEXT,"
            "message TEXT,"
            "timestamp INTEGER,"
            "reply_to_id INTEGER DEFAULT 0);");

    Execute("CREATE TABLE IF NOT EXISTS server_messages ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "server_id INTEGER,"
            "channel_id INTEGER,"
            "username TEXT,"
            "message TEXT,"
            "timestamp INTEGER,"
            "reply_to_id INTEGER DEFAULT 0);");

    Execute("CREATE TABLE IF NOT EXISTS servers ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "name TEXT,"
            "password_hash TEXT,"
            "owner TEXT);");

    Execute("CREATE TABLE IF NOT EXISTS channels ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "server_id INTEGER,"
            "name TEXT,"
            "type INTEGER,"
            "category TEXT DEFAULT 'Uncategorized');");

    Execute("CREATE TABLE IF NOT EXISTS server_members ("
            "server_id INTEGER,"
            "username TEXT,"
            "role INTEGER,"
            "is_muted INTEGER,"
            "UNIQUE(server_id, username));");

    // Migration logic - only run if columns are missing
    auto hasColumn = [&](const std::string& table, const std::string& column) {
        sqlite3_stmt* stmt;
        std::string sql = "PRAGMA table_info(" + table + ")";
        sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        bool found = false;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* name = (const char*)sqlite3_column_text(stmt, 1);
            if (name && column == name) {
                found = true;
                break;
            }
        }
        sqlite3_finalize(stmt);
        return found;
    };

    if (!hasColumn("private_messages", "id")) {
        Execute("CREATE TABLE private_messages_tmp ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "sender TEXT,"
                "receiver TEXT,"
                "message TEXT,"
                "timestamp INTEGER,"
                "reply_to_id INTEGER DEFAULT 0);");
        Execute("INSERT INTO private_messages_tmp (sender, receiver, message, timestamp) "
                "SELECT sender, receiver, message, timestamp FROM private_messages;");
        Execute("DROP TABLE private_messages;");
        Execute("ALTER TABLE private_messages_tmp RENAME TO private_messages;");
    }

    if (!hasColumn("server_messages", "reply_to_id")) {
        Execute("ALTER TABLE server_messages ADD COLUMN reply_to_id INTEGER DEFAULT 0;");
    }
    if (!hasColumn("chat_messages", "reply_to_id")) {
        Execute("ALTER TABLE chat_messages ADD COLUMN reply_to_id INTEGER DEFAULT 0;");
    }

    return true;
}


void Database::Close() {
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
}

bool Database::Execute(const std::string& sql) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
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
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
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
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
    std::vector<ChatMessage> result;

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "SELECT id, username, message, timestamp, reply_to_id FROM chat_messages "
        "ORDER BY id DESC LIMIT ?",
        -1, &stmt, nullptr);

    sqlite3_bind_int(stmt, 1, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* user = (const char*)sqlite3_column_text(stmt, 1);
        const char* msg = (const char*)sqlite3_column_text(stmt, 2);
        result.push_back({
            (uint64_t)sqlite3_column_int64(stmt, 0),
            user ? user : "",
            msg ? msg : "",
            (uint64_t)sqlite3_column_int64(stmt, 3),
            (uint64_t)sqlite3_column_int64(stmt, 4)
            });
    }

    sqlite3_finalize(stmt);
    std::reverse(result.begin(), result.end());
    return result;
}

uint64_t Database::SaveServerMessage(int serverId, int channelId, const std::string& username, const std::string& message, uint64_t timestamp, uint64_t reply_to_id) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "INSERT INTO server_messages(server_id, channel_id, username, message, timestamp, reply_to_id) VALUES(?, ?, ?, ?, ?, ?)",
        -1, &stmt, nullptr);

    sqlite3_bind_int(stmt, 1, serverId);
    sqlite3_bind_int(stmt, 2, channelId);
    sqlite3_bind_text(stmt, 3, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, message.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, timestamp);
    sqlite3_bind_int64(stmt, 6, reply_to_id);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return sqlite3_last_insert_rowid(db);
}

std::vector<ChatMessage> Database::GetServerMessages(int serverId, int channelId, int limit) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
    std::vector<ChatMessage> result;
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "SELECT id, username, message, timestamp, reply_to_id FROM server_messages "
        "WHERE server_id=? AND channel_id=? ORDER BY timestamp DESC LIMIT ?",
        -1, &stmt, nullptr);

    sqlite3_bind_int(stmt, 1, serverId);
    sqlite3_bind_int(stmt, 2, channelId);
    sqlite3_bind_int(stmt, 3, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* user = (const char*)sqlite3_column_text(stmt, 1);
        const char* msg = (const char*)sqlite3_column_text(stmt, 2);
        result.push_back({
            (uint64_t)sqlite3_column_int64(stmt, 0),
            user ? user : "",
            msg ? msg : "",
            (uint64_t)sqlite3_column_int64(stmt, 3),
            (uint64_t)sqlite3_column_int64(stmt, 4)
            });
    }

    sqlite3_finalize(stmt);
    std::reverse(result.begin(), result.end());
    return result;
}

bool Database::EditMessage(uint64_t messageId, const std::string& newContent) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "UPDATE server_messages SET message=? WHERE id=?", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, newContent.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, messageId);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    
    // Also try private_messages if it wasn't a server message
    if (sqlite3_changes(db) == 0) {
        sqlite3_prepare_v2(db, "UPDATE private_messages SET message=? WHERE id=?", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, newContent.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, messageId);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
    }
    return ok;
}

bool Database::DeleteMessage(uint64_t messageId) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "DELETE FROM server_messages WHERE id=?", -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, messageId);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    
    if (sqlite3_changes(db) == 0) {
        sqlite3_prepare_v2(db, "DELETE FROM private_messages WHERE id=?", -1, &stmt, nullptr);
        sqlite3_bind_int64(stmt, 1, messageId);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
    }
    return ok;
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
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
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
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
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
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
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

bool Database::DeleteAccount(const std::string& username) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
    sqlite3_stmt* stmt;
    
    // Note: Since Execute is locked behind our own dbMutex, we shouldn't use it here if already locked, 
    // but Execute doesn't lock dbMutex, so we can use it safely.
    
    // 1. Delete from accounts
    sqlite3_prepare_v2(db, "DELETE FROM accounts WHERE username=?", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_finalize(stmt);
    
    // 2. Delete from sessions
    sqlite3_prepare_v2(db, "DELETE FROM sessions WHERE username=?", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_finalize(stmt);
    
    // 3. Delete friendships
    sqlite3_prepare_v2(db, "DELETE FROM friends WHERE user1=? OR user2=?", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_finalize(stmt);
    
    // 4. Delete private messages
    sqlite3_prepare_v2(db, "DELETE FROM private_messages WHERE sender=? OR receiver=?", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_finalize(stmt);

    // 5. Delete server messages
    sqlite3_prepare_v2(db, "DELETE FROM server_messages WHERE username=?", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_finalize(stmt);

    // 6. Delete from server_members
    sqlite3_prepare_v2(db, "DELETE FROM server_members WHERE username=?", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt); sqlite3_finalize(stmt);
    
    // 7. Delete servers they own, cascading to their channels/members
    sqlite3_prepare_v2(db, "SELECT id FROM servers WHERE owner=?", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    std::vector<int> ownedServers;
    while(sqlite3_step(stmt) == SQLITE_ROW) {
        ownedServers.push_back(sqlite3_column_int(stmt, 0));
    }
    sqlite3_finalize(stmt);
    
    for (int sid : ownedServers) {
        // Since DeleteServer calls Execute which doesn't lock dbMutex, it's safe to do this here
        Execute("DELETE FROM server_members WHERE server_id=" + std::to_string(sid));
        Execute("DELETE FROM channels WHERE server_id=" + std::to_string(sid));
        Execute("DELETE FROM server_messages WHERE server_id=" + std::to_string(sid));
        Execute("DELETE FROM servers WHERE id=" + std::to_string(sid));
    }

    return true;
}



bool Database::SaveSessionToken(const std::string& username, const std::string& token) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
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
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
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
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
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

std::vector<std::string> Database::GetPendingFriendRequests(const std::string& username) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
    std::vector<std::string> out;
    sqlite3_stmt* stmt;

    // Find all users who sent a request to 'username' (user1 = them, user2 = us),
    // but 'username' hasn't sent a request back yet (no row where user1 = us, user2 = them).
    const char* sql = 
        "SELECT f1.user1 FROM friends f1 "
        "WHERE f1.user2 = ? AND NOT EXISTS ("
        "  SELECT 1 FROM friends f2 WHERE f2.user1 = f1.user2 AND f2.user2 = f1.user1"
        ")";

    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.emplace_back((const char*)sqlite3_column_text(stmt, 0));
    }

    sqlite3_finalize(stmt);
    return out;
}

bool Database::AcceptFriendRequest(const std::string& user1, const std::string& user2) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
    bool ok1 = CreateFriendRequest(user1, user2);
    bool ok2 = CreateFriendRequest(user2, user1);
    return ok1 && ok2;
}

bool Database::RemoveFriend(const std::string& u1, const std::string& u2) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
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
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
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
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
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
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
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
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
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

std::vector<std::tuple<int, std::string, std::string>> Database::GetUserServers(const std::string& username) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "SELECT servers.id, servers.name, servers.owner FROM servers "
        "INNER JOIN server_members ON servers.id = server_members.server_id "
        "WHERE server_members.username = ?",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    std::vector<std::tuple<int, std::string, std::string>> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.emplace_back(sqlite3_column_int(stmt, 0), 
                            (const char*)sqlite3_column_text(stmt, 1),
                            (const char*)sqlite3_column_text(stmt, 2));
    }
    sqlite3_finalize(stmt);
    return results;
}

bool Database::DeleteServer(int serverId) {
    Execute("DELETE FROM server_members WHERE server_id=" + std::to_string(serverId) + ";");
    Execute("DELETE FROM channels WHERE server_id=" + std::to_string(serverId) + ";");
    Execute("DELETE FROM server_messages WHERE server_id=" + std::to_string(serverId) + ";");
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
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
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
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
         "DELETE FROM channels WHERE id=?",
         -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, channelId);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::EditChannel(int channelId, const std::string& name, int type, const std::string& category) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "UPDATE channels SET name=?, type=?, category=? WHERE id=?", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, type);
    sqlite3_bind_text(stmt, 3, category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, channelId);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<std::tuple<int, std::string, int, std::string>> Database::GetChannels(int serverId) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
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
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
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
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
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

// Removed void SavePrivateMessage wrapper to avoid ambiguity with uint64_t version

uint64_t Database::SavePrivateMessage(const std::string& from, const std::string& to,
    const std::string& msg, uint64_t ts, uint64_t reply_to_id) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "INSERT INTO private_messages(sender, receiver, message, timestamp, reply_to_id) VALUES(?,?,?,?,?)",
        -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1, from.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, to.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, msg.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, ts);
    sqlite3_bind_int64(stmt, 5, reply_to_id);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return sqlite3_last_insert_rowid(db);
}

std::vector<ChatMessage> Database::GetPrivateMessages(const std::string& u1, const std::string& u2, int limit) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
    std::vector<ChatMessage> out;

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "SELECT id, sender, message, timestamp, reply_to_id FROM private_messages "
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
            (uint64_t)sqlite3_column_int64(stmt, 0),
            (const char*)sqlite3_column_text(stmt, 1),
            (const char*)sqlite3_column_text(stmt, 2),
            (uint64_t)sqlite3_column_int64(stmt, 3),
            (uint64_t)sqlite3_column_int64(stmt, 4)
            });
    }

    sqlite3_finalize(stmt);
    std::reverse(out.begin(), out.end());
    return out;
}

bool Database::UpdateProfile(const std::string& username, const std::string& bio, const std::string& pfp_url) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
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
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
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
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
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

std::pair<std::string, std::string> Database::GetMessageParticipants(uint64_t messageId) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
    sqlite3_stmt* stmt;
    
    // Check server_messages
    sqlite3_prepare_v2(db, "SELECT username FROM server_messages WHERE id=?", -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, messageId);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string sender = (const char*)sqlite3_column_text(stmt, 0);
        sqlite3_finalize(stmt);
        return {sender, ""}; // Server messages only have one "participant" to notify in terms of DMs (not applicable)
    }
    sqlite3_finalize(stmt);

    // Check private_messages
    sqlite3_prepare_v2(db, "SELECT sender, receiver FROM private_messages WHERE id=?", -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, messageId);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string sender = (const char*)sqlite3_column_text(stmt, 0);
        std::string receiver = (const char*)sqlite3_column_text(stmt, 1);
        sqlite3_finalize(stmt);
        return {sender, receiver};
    }
    sqlite3_finalize(stmt);
    return {"", ""};
}

std::vector<std::string> Database::GetDMContacts(const std::string& username) {
    std::lock_guard<std::recursive_mutex> lock(dbMutex);
    sqlite3_stmt* stmt;
    std::vector<std::string> contacts;
    
    const char* sql = "SELECT DISTINCT sender FROM private_messages WHERE receiver = ? "
                      "UNION "
                      "SELECT DISTINCT receiver FROM private_messages WHERE sender = ?;";
                      
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* name = (const char*)sqlite3_column_text(stmt, 0);
            if (name && std::string(name) != username) {
                contacts.push_back(name);
            }
        }
        sqlite3_finalize(stmt);
    }
    return contacts;
}
