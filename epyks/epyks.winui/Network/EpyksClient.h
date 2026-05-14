#pragma once
#include "../pch.h"

// Pull in our shared protocol structs
#include "../../epyks.Core/src/Protocol/Packet.h"

namespace epyks_winui
{
    // ---------------------------------------------------------------
    // Plain data structs (no WinRT) — mirrored from the old main.cpp
    // ---------------------------------------------------------------
    struct MsgModel
    {
        uint64_t    id = 0;
        uint64_t    replyToId = 0;
        std::string sender;
        std::string content;
    };

    struct ChannelModel
    {
        int         id = 0;
        std::string name;
        int         type = 0;   // 0=text, 1=voice
        std::string category;
    };

    struct ServerMember
    {
        std::string username;
        std::string pfp_url;
        int         voice_channel_id = -1;
        bool        is_muted = false;
        bool        is_talking = false;
        bool        online = false;
    };

    struct ServerModel
    {
        int         id = 0;
        std::string name;
        std::string owner;
        std::vector<ChannelModel> channels;
        std::vector<ServerMember> members;
        std::map<int, std::vector<MsgModel>> channelMessages;
    };

    struct FriendEntry
    {
        std::string username;
        bool        online = false;
        bool        hasUnread = false;
    };

    struct DMChat
    {
        std::string username;
        std::vector<MsgModel> messages;
    };

    // ---------------------------------------------------------------
    // Callback typedefs
    // ---------------------------------------------------------------
    using LoginResultCb     = std::function<void(bool success, std::string error, std::string token)>;
    using RegisterResultCb  = std::function<void(bool success, std::string error)>;
    using ServerMsgCb       = std::function<void(int serverId, int channelId, MsgModel msg)>;
    using PrivateMsgCb      = std::function<void(std::string partner, MsgModel msg)>;
    using FriendListCb      = std::function<void(std::vector<FriendEntry>)>;
    using FriendRequestCb   = std::function<void(std::string from)>;
    using MyServersCb       = std::function<void(std::map<int, ServerModel>)>;
    using ChannelListCb     = std::function<void(int serverId, std::vector<ChannelModel>)>;
    using ProfileDataCb     = std::function<void(epyks::UserProfile)>;
    using DisconnectedCb    = std::function<void()>;
    using AvailServersCb    = std::function<void(std::vector<std::tuple<int,std::string,bool>>)>;
    using HistoryCb         = std::function<void(int serverId, int channelId, std::vector<MsgModel>)>;
    using MyDmsCb           = std::function<void(std::vector<std::string>)>;
    using OpStatusCb        = std::function<void(std::string status)>;

    // ---------------------------------------------------------------
    // EpyksClient — networking singleton
    // ---------------------------------------------------------------
    class EpyksClient
    {
    public:
        EpyksClient();
        ~EpyksClient();

        // --- Connection ---
        bool Connect(const char* host, int port);
        void Disconnect();
        bool IsConnected() const { return m_connected; }

        // --- Auth ---
        void Login(const std::string& username, const std::string& password);
        void Register(const std::string& username, const std::string& password);
        void TokenLogin(const std::string& username, const std::string& token);
        void Logout();

        // --- Messaging ---
        void SendServerMessage(int serverId, int channelId, const std::string& text, uint64_t replyTo = 0);
        void SendPrivateMessage(const std::string& to, const std::string& text, uint64_t replyTo = 0);
        void EditMessage(int serverId, int channelId, uint64_t msgId, const std::string& newText);
        void DeleteMessage(int serverId, int channelId, uint64_t msgId);
        void RequestServerHistory(int serverId, int channelId);

        // --- Friends ---
        void SendFriendRequest(const std::string& target);
        void RespondFriendRequest(const std::string& from, bool accept);
        void Unfriend(const std::string& target);
        void RequestFriendList();
        void RequestMyDms();
        void KickUser(int serverId, const std::string& target);


        // --- Servers ---
        void RequestMyServers();
        void SendCreateServer(const std::string& name, const std::string& password = "");
        void SendJoinServer(int serverId, const std::string& password = "");
        void SendLeaveServer(int serverId);
        void SendListServers();
        void RenameServer(int serverId, const std::string& newName);

        // --- Channels ---
        void CreateChannel(int serverId, const std::string& name, int type, const std::string& category = "");
        void DeleteChannel(int serverId, int channelId);
        void RequestChannelList(int serverId);
        void RequestMemberList(int serverId);

        // --- Profile ---
        void RequestProfile(const std::string& username, bool silent = false);
        void UpdateProfile(const std::string& bio, const std::string& displayName);
        void UploadPfp(const std::vector<uint8_t>& imageData);

        // --- Voice ---
        void JoinVoice(int serverId, int channelId);
        void LeaveVoice();
        void SendVoiceData(int serverId, int channelId, const std::vector<uint8_t>& audio);

        // --- Media ---
        void UploadMedia(const std::vector<uint8_t>& data);
        void RequestMedia(const std::string& filename);

        // --- State ---
        std::string Username() const { return m_username; }
        std::string SessionToken() const { return m_sessionToken; }

        // --- Callbacks (set these before Connect) ---
        LoginResultCb   OnLoginResult;
        RegisterResultCb OnRegisterResult;
        ServerMsgCb     OnServerMessage;
        PrivateMsgCb    OnPrivateMessage;
        FriendListCb    OnFriendList;
        FriendRequestCb OnFriendRequest;
        MyServersCb     OnMyServers;
        std::function<void(int)> OnJoinServer;
        ChannelListCb   OnChannelList;
        std::function<void(int, std::vector<ServerMember>)> OnMemberList;
        ProfileDataCb   OnProfileData;
        DisconnectedCb  OnDisconnected;
        AvailServersCb  OnAvailableServers;
        MyDmsCb         OnMyDms;
        OpStatusCb      OnOpStatus;         // Generic operation status (rename server, etc.)
        HistoryCb       OnHistory;          // Fired when server history is received
        std::function<void()> OnChannelListNeeded; // Fired after CREATE_CHANNEL to refresh channel list

        // Media received callback: (filename, bytes)
        std::function<void(std::string, std::vector<uint8_t>)> OnMediaReceived;
        // Fired when a pending media upload gets its server URL
        std::function<void(std::string)> OnMediaUploadComplete;
        // PFP received: (username, bytes)
        std::function<void(std::string, std::vector<uint8_t>)> OnPfpReceived;
        // Voice data received: (username, audio_bytes)
        std::function<void(std::string, std::vector<uint8_t>)> OnVoiceData;

        // Pending profile request (to suppress auto-shown profile)
        std::string m_pendingProfileRequest;
        bool        m_pendingProfileSilent = false;
        std::string m_pendingMediaUploadUrl;

    private:
        void SendPacket(const epyks::Packet& packet);
        void ReceiveLoop();
        void HandlePacket(const epyks::Packet& packet);

        SOCKET          m_sock = INVALID_SOCKET;
        std::atomic<bool> m_connected{ false };
        std::thread     m_recvThread;
        std::mutex      m_sendMutex;
        std::string     m_username;
        std::string     m_sessionToken;
        std::string     m_serverIp;
        int             m_serverPort = 0;
    };

    // Global singleton
    EpyksClient& GetClient();
}
