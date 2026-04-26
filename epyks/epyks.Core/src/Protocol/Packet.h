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

	struct CreateServer {
		std::string server_name;
		std::string password;
        std::vector<uint8_t> Serialize() const;
		bool Deserialize(const std::vector<uint8_t>& bytes);
	};

    struct LeaveServer {
        int server_id;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
	};

    struct JoinServer {
        int server_id;
        std::string password;
        std::vector<uint8_t> Serialize() const;
		bool Deserialize(const std::vector<uint8_t>& bytes);
	};

    struct ServerMessage {
        int server_id;
        int channel_id;
        std::string content;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
	};

    struct CreateChannel {
        int server_id;
        std::string channel_name;
        int type;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct DeleteChannel {
        int server_id;
        int channel_id;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct KickUser {
        int server_id;
        std::string target_username;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct MuteUser {
        int server_id;
        std::string target_username;
        bool is_muted;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct ChannelList {
        int server_id;
        std::string data;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct JoinVoice {
        int server_id;
        int channel_id;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct LeaveVoice {
        int server_id;
        int channel_id;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct VoiceData {
        std::string username;
        int server_id;
        int channel_id;
        std::vector<uint8_t> audio_data;
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

        // Servers
		constexpr uint32_t CREATE_SERVER = 40;
		constexpr uint32_t JOIN_SERVER = 41;
		constexpr uint32_t LEAVE_SERVER = 42;
        constexpr uint32_t SERVER_MESSAGE = 43;
        constexpr uint32_t LIST_SERVERS = 44;
        constexpr uint32_t MY_SERVERS = 45;

        // Channels
        constexpr uint32_t CREATE_CHANNEL = 50;
        constexpr uint32_t DELETE_CHANNEL = 51;
        constexpr uint32_t CHANNEL_LIST = 52;
        
        // Mod
        constexpr uint32_t KICK_USER = 60;
        constexpr uint32_t MUTE_USER = 61;
        
        // Voice
        constexpr uint32_t JOIN_VOICE = 70;
        constexpr uint32_t LEAVE_VOICE = 71;
        constexpr uint32_t VOICE_DATA = 72;
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