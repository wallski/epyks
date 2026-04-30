#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>

namespace epyks {
    enum class PacketType : uint32_t {
        LOGIN = 1,
        REGISTER = 2,
        AUTH_RESPONSE = 3,
        FRIEND_REQUEST = 4,
        FRIEND_LIST = 5,
        PRIVATE_MESSAGE = 6,
        MY_SERVERS = 7,
        JOIN_SERVER = 8,
        CREATE_SERVER = 9,
        LIST_SERVERS = 10,
        SERVER_CHANNELS = 11,
        HISTORY = 12,
        JOIN_VOICE = 13,
        LEAVE_VOICE = 14,
        VOICE_DATA = 15,
        PFP_REQUEST = 16,
        PFP_RESPONSE = 17,
        UPDATE_PROFILE = 18,
        SEARCH_USER = 19,
        CREATE_CHANNEL = 20,
        DELETE_CHANNEL = 21,
        EDIT_CHANNEL = 22,
        KICK_USER = 23,
        MUTE_USER = 24,
        SERVER_MEMBERS = 25,
        LEAVE_SERVER = 26,
        EDIT_MESSAGE = 27,
        DELETE_MESSAGE = 28,
        DELETE_ACCOUNT = 29,
        TOKEN_LOGIN = 30,
        TOKEN_LOGIN_RESPONSE = 31,
        REGISTER_RESPONSE = 32,
        LOGIN_RESPONSE = 33,
        SERVER_MESSAGE = 34,
        CHAT_MESSAGE = 35,
        FRIEND_RESPONSE = 36,
        PROFILE_UPDATE = 37,
        PROFILE_DATA = 38,
        MEDIA_UPLOAD = 39,
        MEDIA_RESPONSE = 40,
        MEDIA_REQUEST = 41,
        GET_PROFILE = 42,
        MEMBER_LIST_REQUEST = 43,
        MEMBER_LIST_RESPONSE = 44,
        RENAME_SERVER = 45,
        CHANNEL_LIST = 46,
        JOIN_LEAVE = 47,
        UNFRIEND = 48,
        AUTH_PROMPT = 49,
        ONLINE_STATUS = 50,
        PFP_UPLOAD = 51,
        PROFILE_DATA_RESPONSE = 52,
        CHANNEL_HISTORY = 53,
        MY_DMS = 54
    };

    struct Packet {
        PacketType type;
        uint64_t timestamp;
        std::string data;
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
        bool success;
        uint64_t user_id;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct FriendRequest {
        std::string target_username;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct FriendResponse {
        bool accepted;
        std::string target_username;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct PrivateMessage {
        std::string target_username;
        std::string sender_username;
        std::string content;
        uint64_t message_id = 0;
        uint64_t reply_to_id = 0;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct FriendList {
        std::vector<std::string> usernames;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

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

    struct LoginResponse {
        bool success;
        std::string error;
        std::string session_token;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct CreateServer {
        std::string server_name;
        std::string password;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct JoinServer {
        int server_id;
        std::string password;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct LeaveServer {
        int server_id;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct ServerMessage {
        int server_id;
        int channel_id;
        std::string username;
        std::string content;
        uint64_t message_id = 0;
        uint64_t reply_to_id = 0;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct DeleteMessage {
        int server_id;
        int channel_id;
        uint64_t message_id;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct EditMessage {
        int server_id;
        int channel_id;
        uint64_t message_id;
        std::string new_content;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct CreateChannel {
        int server_id;
        int type;
        std::string channel_name;
        std::string category;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct EditChannel {
        int server_id;
        int channel_id;
        int type;
        std::string name;
        std::string category;
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

    struct TokenLoginRequest {
        std::string username;
        std::string token;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct UserProfile {
        std::string username;
        std::string bio;
        std::string pfp_url;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct ProfileUpdate {
        std::string bio;
        std::string pfp_url;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct MemberInfo {
        std::string username;
        std::string bio;
        std::string pfp_url;
        int role;
        int voice_channel_id;
        bool is_muted;
        bool is_talking;
    };

    struct MemberListRequest {
        int server_id;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct MemberListResponse {
        int server_id;
        std::vector<MemberInfo> members;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct PfpUpload {
        std::vector<uint8_t> image_data;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct PfpRequest {
        std::string username;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct PfpResponse {
        std::string username;
        std::vector<uint8_t> image_data;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct MediaRequest {
        std::string filename;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct MediaUpload {
        std::vector<uint8_t> media_data;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct MediaResponse {
        std::string url;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct DeleteAccount {
        std::string password;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };

    struct Unfriend {
        std::string target_username;
        std::vector<uint8_t> Serialize() const;
        bool Deserialize(const std::vector<uint8_t>& bytes);
    };
}