#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace epyks {

    struct Packet {
        uint32_t type = 0;
        std::string data;
        uint64_t timestamp = 0;

        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct Auth {
        std::string username;
        std::string password;

        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct AuthResponse {
        bool success = false;
        uint64_t user_id = 0;

        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct FriendList {
        std::vector<std::string> usernames;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
	};

    struct FriendRequest {
        std::string target_username;

        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct Unfriend {
        std::string target_username;

        std::vector<uint8_t> Serialize() const {
            std::vector<uint8_t> out;
            uint32_t len = target_username.size();
            out.insert(out.end(), (uint8_t*)&len, (uint8_t*)&len + 4);
            out.insert(out.end(), target_username.begin(), target_username.end());
            return out;
        }

        bool Deserialize(const std::vector<uint8_t>& data) {
            if (data.size() < 4) return false;
            uint32_t len = *(uint32_t*)data.data();
            if (data.size() < 4 + len) return false;
            target_username = std::string(data.begin() + 4, data.begin() + 4 + len);
            return true;
        }
    };

    struct FriendResponse {
        bool accepted = false;
        std::string target_username;

        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct PrivateMessage {
        std::string target_username;
        std::string content;

        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

	struct CreateGroup {
		std::string group_name;
        std::vector<uint8_t> Serialize() const;
		bool Deserialize(const std::vector<uint8_t>& bytes);
	};

    struct LeaveGroup{
        int group_id;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
	};

    struct JoinGroup{
        int group_id;
        std::vector<uint8_t> Serialize() const;
		bool Deserialize(const std::vector<uint8_t>& bytes);
	};

    struct GroupMessage {
        int group_id;
        std::string content;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
	};



    // Packet type constants
    namespace PacketType {
        constexpr uint32_t TOKEN_LOGIN = 34;
        constexpr uint32_t TOKEN_LOGIN_RESPONSE = 35;
        constexpr uint32_t AUTH_PROMPT = 1;
        constexpr uint32_t USERNAME = 2;
        constexpr uint32_t JOIN_LEAVE = 3;
        constexpr uint32_t CHAT_MESSAGE = 10;
        constexpr uint32_t HISTORY = 11;

        // Friends
        constexpr uint32_t FRIEND_REQUEST = 20;
        constexpr uint32_t FRIEND_RESPONSE = 21;
        constexpr uint32_t PRIVATE_MESSAGE = 22;
        constexpr uint32_t FRIEND_LIST = 23;
        constexpr uint32_t ONLINE_STATUS = 24;
        constexpr uint32_t UNFRIEND = 25;
      
        // Accounts
        constexpr uint32_t REGISTER = 30;
        constexpr uint32_t REGISTER_RESPONSE = 31;
        constexpr uint32_t LOGIN = 32;
        constexpr uint32_t LOGIN_RESPONSE = 33;

        //groups
		constexpr uint32_t CREATE_GROUP = 40;
		constexpr uint32_t JOIN_GROUP = 41;
		constexpr uint32_t LEAVE_GROUP = 42;
        constexpr uint32_t GROUP_MESSAGE = 43;
        constexpr uint32_t LIST_GROUPS = 44;
        constexpr uint32_t MY_GROUPS = 45;
    }

    struct RegisterRequest {
        std::string username;
        std::string password;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct RegisterResponse {
        bool success;
        std::string error;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct LoginRequest {
        std::string username;
        std::string password;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct TokenLoginRequest {
        std::string username;
        std::string token;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct LoginResponse {
        bool success = false;
        std::string error;
        std::string session_token;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

} // namespace epyks