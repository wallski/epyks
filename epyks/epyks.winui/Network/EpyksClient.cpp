#include "pch.h"
#include "EpyksClient.h"

#pragma comment(lib, "ws2_32.lib")

namespace epyks_winui
{
    // Global singleton
    static EpyksClient g_client;
    EpyksClient& GetClient() { return g_client; }

    EpyksClient::EpyksClient()
    {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }

    EpyksClient::~EpyksClient()
    {
        Disconnect();
        WSACleanup();
    }

    // ---------------------------------------------------------------
    // Connection
    // ---------------------------------------------------------------
    bool EpyksClient::Connect(const char* host, int port)
    {
        if (m_connected && m_serverIp == host && m_serverPort == port)
            return true;

        Disconnect();

        struct addrinfo hints = {}, *res;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        std::string portStr = std::to_string(port);
        if (getaddrinfo(host, portStr.c_str(), &hints, &res) != 0)
            return false;

        m_sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (connect(m_sock, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR)
        {
            freeaddrinfo(res);
            closesocket(m_sock);
            m_sock = INVALID_SOCKET;
            return false;
        }
        freeaddrinfo(res);

        m_connected = true;
        m_serverIp = host;
        m_serverPort = port;
        m_recvThread = std::thread(&EpyksClient::ReceiveLoop, this);
        return true;
    }

    void EpyksClient::Disconnect()
    {
        m_connected = false;
        if (m_sock != INVALID_SOCKET)
        {
            closesocket(m_sock);
            m_sock = INVALID_SOCKET;
        }
        if (m_recvThread.joinable())
            m_recvThread.join();
        if (OnDisconnected) OnDisconnected();
    }

    // ---------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------
    void EpyksClient::SendPacket(const epyks::Packet& packet)
    {
        if (!m_connected) return;
        std::lock_guard<std::mutex> lk(m_sendMutex);
        auto data = packet.Serialize();
        uint32_t len = (uint32_t)data.size();
        send(m_sock, (char*)&len, 4, 0);
        send(m_sock, (char*)data.data(), len, 0);
    }

    // ---------------------------------------------------------------
    // Auth
    // ---------------------------------------------------------------
    void EpyksClient::Login(const std::string& username, const std::string& password)
    {
        m_username = username;
        epyks::Auth auth;
        auth.username = username;
        auth.password = password;
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::LOGIN;
        auto b = auth.Serialize();
        pkt.data = std::string(b.begin(), b.end());
        SendPacket(pkt);
    }

    void EpyksClient::Register(const std::string& username, const std::string& password)
    {
        m_username = username;
        epyks::Auth auth;
        auth.username = username;
        auth.password = password;
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::REGISTER;
        auto b = auth.Serialize();
        pkt.data = std::string(b.begin(), b.end());
        SendPacket(pkt);
    }

    void EpyksClient::TokenLogin(const std::string& username, const std::string& token)
    {
        m_username = username;
        epyks::TokenLogin tl;
        tl.username = username;
        tl.token = token;
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::TOKEN_LOGIN;
        auto b = tl.Serialize();
        pkt.data = std::string(b.begin(), b.end());
        SendPacket(pkt);
    }

    void EpyksClient::Logout()
    {
        m_sessionToken.clear();
        Disconnect();
    }

    // ---------------------------------------------------------------
    // Messaging
    // ---------------------------------------------------------------
    void EpyksClient::SendServerMessage(int serverId, int channelId, const std::string& text, uint64_t replyTo)
    {
        epyks::ServerMessage sm;
        sm.server_id = serverId;
        sm.channel_id = channelId;
        sm.content = text;
        sm.reply_to_id = replyTo;
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::SERVER_MESSAGE;
        pkt.timestamp = GetTickCount64();
        auto b = sm.Serialize();
        pkt.data = std::string(b.begin(), b.end());
        SendPacket(pkt);
    }

    void EpyksClient::SendPrivateMessage(const std::string& to, const std::string& text, uint64_t replyTo)
    {
        epyks::PrivateMessage pm;
        pm.target_username = to;
        pm.sender_username = m_username;
        pm.content = text;
        pm.reply_to_id = replyTo;
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::PRIVATE_MESSAGE;
        pkt.timestamp = GetTickCount64();
        auto b = pm.Serialize();
        pkt.data = std::string(b.begin(), b.end());
        SendPacket(pkt);
    }

    void EpyksClient::EditMessage(int serverId, int channelId, uint64_t msgId, const std::string& newText)
    {
        epyks::EditMessage em;
        em.server_id = serverId;
        em.channel_id = channelId;
        em.message_id = msgId;
        em.new_content = newText;
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::EDIT_MESSAGE;
        auto b = em.Serialize();
        pkt.data = std::string(b.begin(), b.end());
        SendPacket(pkt);
    }

    void EpyksClient::DeleteMessage(int serverId, int channelId, uint64_t msgId)
    {
        epyks::DeleteMessage dm;
        dm.server_id = serverId;
        dm.channel_id = channelId;
        dm.message_id = msgId;
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::DELETE_MESSAGE;
        auto b = dm.Serialize();
        pkt.data = std::string(b.begin(), b.end());
        SendPacket(pkt);
    }

    void EpyksClient::RequestServerHistory(int serverId, int channelId)
    {
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::HISTORY;
        pkt.data = std::to_string(serverId) + "|" + std::to_string(channelId);
        SendPacket(pkt);
    }

    // ---------------------------------------------------------------
    // Friends
    // ---------------------------------------------------------------
    void EpyksClient::SendFriendRequest(const std::string& target)
    {
        epyks::FriendRequest fr;
        fr.target_username = target;
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::FRIEND_REQUEST;
        auto b = fr.Serialize();
        pkt.data = std::string(b.begin(), b.end());
        SendPacket(pkt);
    }

    void EpyksClient::RespondFriendRequest(const std::string& from, bool accept)
    {
        epyks::FriendResponse fr;
        fr.target_username = from;
        fr.accepted = accept;
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::FRIEND_RESPONSE;
        auto b = fr.Serialize();
        pkt.data = std::string(b.begin(), b.end());
        SendPacket(pkt);
    }


    void EpyksClient::RequestFriendList()
    {
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::FRIEND_LIST;
        SendPacket(pkt);
    }

    void EpyksClient::RequestMyDms()
    {
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::MY_DMS;
        SendPacket(pkt);
    }

    // ---------------------------------------------------------------
    // Servers
    // ---------------------------------------------------------------
    void EpyksClient::RequestMyServers()
    {
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::MY_SERVERS;
        SendPacket(pkt);
    }

    void EpyksClient::SendCreateServer(const std::string& name, const std::string& password)
    {
        epyks::CreateServer cs;
        cs.server_name = name;
        cs.password = password;
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::CREATE_SERVER;
        auto b = cs.Serialize();
        pkt.data = std::string(b.begin(), b.end());
        SendPacket(pkt);
    }

    void EpyksClient::SendJoinServer(int serverId, const std::string& password)
    {
        epyks::JoinServer js;
        js.server_id = serverId;
        js.password = password;
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::JOIN_SERVER;
        auto b = js.Serialize();
        pkt.data = std::string(b.begin(), b.end());
        SendPacket(pkt);
    }

    void EpyksClient::SendLeaveServer(int serverId)
    {
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::LEAVE_SERVER;
        pkt.data = std::to_string(serverId);
        SendPacket(pkt);
    }

    void EpyksClient::SendListServers()
    {
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::LIST_SERVERS;
        SendPacket(pkt);
    }

    void EpyksClient::RenameServer(int serverId, const std::string& newName)
    {
        epyks::RenameServer rs;
        rs.server_id = serverId;
        rs.new_name = newName;
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::RENAME_SERVER;
        auto b = rs.Serialize();
        pkt.data = std::string(b.begin(), b.end());
        SendPacket(pkt);
    }

    // ---------------------------------------------------------------
    // Channels
    // ---------------------------------------------------------------
    void EpyksClient::CreateChannel(int serverId, const std::string& name, int type, const std::string& category)
    {
        epyks::CreateChannel cc;
        cc.server_id = serverId;
        cc.channel_name = name;
        cc.type = type;
        cc.category = category;
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::CREATE_CHANNEL;
        auto b = cc.Serialize();
        pkt.data = std::string(b.begin(), b.end());
        SendPacket(pkt);
    }

    void EpyksClient::DeleteChannel(int serverId, int channelId)
    {
        epyks::DeleteChannel dc;
        dc.server_id = serverId;
        dc.channel_id = channelId;
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::DELETE_CHANNEL;
        auto b = dc.Serialize();
        pkt.data = std::string(b.begin(), b.end());
        SendPacket(pkt);
    }

    void EpyksClient::RequestChannelList(int serverId)
    {
        epyks::ChannelList req;
        req.server_id = serverId;
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::CHANNEL_LIST;
        auto b = req.Serialize();
        pkt.data = std::string(b.begin(), b.end());
        SendPacket(pkt);
    }

    void EpyksClient::RequestMemberList(int serverId)
    {
        epyks::MemberListRequest req;
        req.server_id = serverId;
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::MEMBER_LIST_REQUEST;
        auto b = req.Serialize();
        pkt.data = std::string(b.begin(), b.end());
        SendPacket(pkt);
    }

    // ---------------------------------------------------------------
    // Profile
    // ---------------------------------------------------------------
    void EpyksClient::RequestProfile(const std::string& username, bool silent)
    {
        m_pendingProfileRequest = username.empty() ? m_username : username;
        m_pendingProfileSilent = silent;
        epyks::GetProfile gp;
        gp.username = m_pendingProfileRequest;
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::GET_PROFILE;
        auto b = gp.Serialize();
        pkt.data = std::string(b.begin(), b.end());
        SendPacket(pkt);
    }

    void EpyksClient::UpdateProfile(const std::string& bio, const std::string& displayName)
    {
        epyks::ProfileUpdate pu;
        pu.bio = bio;
        pu.display_name = displayName;
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::PROFILE_UPDATE;
        auto b = pu.Serialize();
        pkt.data = std::string(b.begin(), b.end());
        SendPacket(pkt);
    }

    void EpyksClient::UploadPfp(const std::vector<uint8_t>& imageData)
    {
        epyks::PfpUpload pu;
        pu.image_data = imageData;
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::PFP_UPLOAD;
        auto b = pu.Serialize();
        pkt.data = std::string(b.begin(), b.end());
        SendPacket(pkt);
    }

    // ---------------------------------------------------------------
    // Voice
    // ---------------------------------------------------------------
    void EpyksClient::JoinVoice(int serverId, int channelId)
    {
        epyks::JoinVoice jv;
        jv.server_id = serverId;
        jv.channel_id = channelId;
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::JOIN_VOICE;
        auto b = jv.Serialize();
        pkt.data = std::string(b.begin(), b.end());
        SendPacket(pkt);
    }

    void EpyksClient::LeaveVoice()
    {
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::LEAVE_VOICE;
        SendPacket(pkt);
    }

    void EpyksClient::SendVoiceData(int serverId, int channelId, const std::vector<uint8_t>& audio)
    {
        epyks::VoiceData vd;
        vd.server_id = serverId;
        vd.channel_id = channelId;
        vd.audio_data = audio;
        vd.username = m_username;
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::VOICE_DATA;
        auto b = vd.Serialize();
        pkt.data = std::string(b.begin(), b.end());
        SendPacket(pkt);
    }

    // ---------------------------------------------------------------
    // Social
    // ---------------------------------------------------------------
    void EpyksClient::Unfriend(const std::string& target)
    {
        epyks::Unfriend u;
        u.target_username = target;
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::UNFRIEND;
        auto b = u.Serialize();
        pkt.data = std::string(b.begin(), b.end());
        SendPacket(pkt);
    }

    void EpyksClient::KickUser(int serverId, const std::string& target)
    {
        epyks::KickUser ku;
        ku.server_id = serverId;
        ku.target_username = target;
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::KICK_USER;
        auto b = ku.Serialize();
        pkt.data = std::string(b.begin(), b.end());
        SendPacket(pkt);
    }

    // ---------------------------------------------------------------
    // Media
    // ---------------------------------------------------------------
    void EpyksClient::UploadMedia(const std::vector<uint8_t>& data)
    {
        epyks::MediaUpload mu;
        mu.media_data = data;
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::MEDIA_UPLOAD;
        auto b = mu.Serialize();
        pkt.data = std::string(b.begin(), b.end());
        m_pendingMediaUploadUrl = "uploading";
        SendPacket(pkt);
    }

    void EpyksClient::RequestMedia(const std::string& filename)
    {
        epyks::MediaRequest mr;
        mr.filename = filename;
        epyks::Packet pkt;
        pkt.type = epyks::PacketType::MEDIA_REQUEST;
        auto b = mr.Serialize();
        pkt.data = std::string(b.begin(), b.end());
        SendPacket(pkt);
    }

    // ---------------------------------------------------------------
    // Receive Loop + Packet Dispatch
    // ---------------------------------------------------------------
    void EpyksClient::ReceiveLoop()
    {
        while (m_connected)
        {
            uint32_t len = 0;
            if (recv(m_sock, (char*)&len, 4, MSG_WAITALL) != 4) break;
            if (len > 10'000'000u) break;

            std::vector<uint8_t> buffer(len);
            int received = 0;
            while (received < (int)len)
            {
                int r = recv(m_sock, (char*)buffer.data() + received, len - received, 0);
                if (r <= 0) { m_connected = false; goto done; }
                received += r;
            }

            epyks::Packet pkt;
            if (!pkt.Deserialize(buffer)) continue;
            HandlePacket(pkt);
        }
    done:
        m_connected = false;
        if (OnDisconnected) OnDisconnected();
    }

    void EpyksClient::HandlePacket(const epyks::Packet& packet)
    {
        auto bytes = [&]() { return std::vector<uint8_t>(packet.data.begin(), packet.data.end()); };

        switch (packet.type)
        {
        // ---- Auth ----
        case epyks::PacketType::LOGIN_RESPONSE:
        case epyks::PacketType::TOKEN_LOGIN_RESPONSE:
        {
            epyks::LoginResponse resp;
            if (resp.Deserialize(bytes()))
            {
                if (resp.success) m_sessionToken = resp.session_token;
                if (OnLoginResult) OnLoginResult(resp.success, resp.error, resp.session_token);
                if (resp.success)
                {
                    RequestMyServers();
                    RequestFriendList();
                    RequestMyDms();
                    RequestProfile("", true);
                }
            }
            break;
        }
        case epyks::PacketType::REGISTER_RESPONSE:
        {
            epyks::RegisterResponse resp;
            if (resp.Deserialize(bytes()))
            {
                if (OnRegisterResult) OnRegisterResult(resp.success, resp.error);
            }
            break;
        }

        // ---- Messages ----
        case epyks::PacketType::SERVER_MESSAGE:
        {
            epyks::ServerMessage sm;
            if (sm.Deserialize(bytes()))
            {
                MsgModel m{ sm.message_id, sm.reply_to_id, sm.username, sm.content };
                if (OnServerMessage) OnServerMessage(sm.server_id, sm.channel_id, m);
            }
            break;
        }
        case epyks::PacketType::PRIVATE_MESSAGE:
        {
            epyks::PrivateMessage pm;
            if (pm.Deserialize(bytes()))
            {
                std::string partner = (pm.sender_username == m_username) ? pm.target_username : pm.sender_username;
                MsgModel m{ pm.message_id, pm.reply_to_id, pm.sender_username, pm.content };
                if (OnPrivateMessage) OnPrivateMessage(partner, m);
            }
            break;
        }

        // ---- Friends ----
        case epyks::PacketType::FRIEND_LIST:
        {
            epyks::FriendList fl;
            if (fl.Deserialize(bytes()))
            {
                std::vector<FriendEntry> entries;
                for (auto& n : fl.usernames) entries.push_back({ n });
                if (OnFriendList) OnFriendList(entries);
            }
            break;
        }
        case epyks::PacketType::FRIEND_REQUEST:
            if (OnFriendRequest) OnFriendRequest(packet.data);
            break;

        // ---- Servers ----
        case epyks::PacketType::MY_SERVERS:
        {
            std::map<int, ServerModel> servers;
            std::stringstream ss(packet.data);
            std::string token;
            while (std::getline(ss, token, ','))
            {
                if (token.empty()) continue;
                size_t c1 = token.find(':'), c2 = token.rfind(':');
                if (c1 != std::string::npos && c2 != c1)
                {
                    int id = std::stoi(token.substr(0, c1));
                    std::string name = token.substr(c1 + 1, c2 - c1 - 1);
                    std::string owner = token.substr(c2 + 1);
                    servers[id] = { id, name, owner };
                    RequestChannelList(id);
                }
            }
            if (OnMyServers) OnMyServers(servers);
            break;
        }
        case epyks::PacketType::CHANNEL_LIST:
        {
            epyks::ChannelList cl;
            if (cl.Deserialize(bytes()))
            {
                std::vector<ChannelModel> channels;
                std::stringstream ss(cl.data);
                std::string token;
                while (std::getline(ss, token, ','))
                {
                    if (token.empty()) continue;
                    std::stringstream cs(token);
                    std::string idS, nameS, typeS, catS;
                    if (std::getline(cs, idS, ':') && std::getline(cs, nameS, ':') &&
                        std::getline(cs, typeS, ':') && std::getline(cs, catS, ':'))
                    {
                        channels.push_back({ std::stoi(idS), nameS, std::stoi(typeS), catS });
                    }
                }
                if (OnChannelList) OnChannelList(cl.server_id, channels);
            }
            break;
        }
        case epyks::PacketType::MEMBER_LIST_RESPONSE:
        {
            epyks::MemberListResponse res;
            if (res.Deserialize(bytes()))
            {
                std::vector<ServerMember> out;
                for (auto& m : res.members)
                {
                    ServerMember sm;
                    sm.username = m.username;
                    sm.pfp_url = m.pfp_url;
                    sm.voice_channel_id = m.voice_channel_id;
                    sm.is_muted = m.is_muted;
                    sm.is_talking = m.is_talking;
                    sm.online = true; // They are in the list so they are online
                    out.push_back(sm);
                }
                if (OnMemberList) OnMemberList(res.server_id, out);
            }
            break;
        }
        case epyks::PacketType::JOIN_SERVER:
        {
            epyks::JoinServer res;
            if (res.Deserialize(bytes())) {
                if (OnJoinServer) OnJoinServer(res.server_id);
            }
            RequestMyServers();
            break;
        }
        case epyks::PacketType::LIST_SERVERS:
        {
            std::vector<std::tuple<int, std::string, bool>> avail;
            std::stringstream ss(packet.data);
            std::string token;
            while (std::getline(ss, token, ','))
            {
                if (token.empty()) continue;
                std::stringstream ts(token);
                std::string idS, nameS, passS;
                if (std::getline(ts, idS, ':') && std::getline(ts, nameS, ':') && std::getline(ts, passS, ':'))
                    avail.push_back({ std::stoi(idS), nameS, passS == "1" });
            }
            if (OnAvailableServers) OnAvailableServers(avail);
            break;
        }
        case epyks::PacketType::RENAME_SERVER:
        case epyks::PacketType::CREATE_SERVER:
        case epyks::PacketType::FRIEND_RESPONSE:
        case epyks::PacketType::LEAVE_SERVER:
        case epyks::PacketType::UNFRIEND:
            if (OnOpStatus) OnOpStatus(packet.data);
            break;
        case epyks::PacketType::CREATE_CHANNEL:
        case epyks::PacketType::DELETE_CHANNEL:
        case epyks::PacketType::EDIT_CHANNEL:
            if (OnOpStatus) OnOpStatus(packet.data);
            // Re-fetch channel list for the current server so changes appear
            if (OnChannelListNeeded) OnChannelListNeeded();
            break;
        case epyks::PacketType::VOICE_DATA:
        {
            epyks::VoiceData vd;
            if (vd.Deserialize(bytes()))
            {
                if (OnVoiceData) OnVoiceData(vd.username, vd.audio_data);
            }
            break;
        }

        // ---- Profile ----
        case epyks::PacketType::PROFILE_DATA:
        {
            epyks::UserProfile profile;
            if (profile.Deserialize(bytes()))
            {
                if (OnProfileData) OnProfileData(profile);
            }
            break;
        }

        // ---- DMs ----
        case epyks::PacketType::MY_DMS:
        {
            std::vector<std::string> dms;
            std::stringstream ss(packet.data);
            std::string tok;
            while (std::getline(ss, tok, ','))
                if (!tok.empty()) dms.push_back(tok);
            if (OnMyDms) OnMyDms(dms);
            break;
        }

        // ---- Media ----
        case epyks::PacketType::MEDIA_RESPONSE:
        {
            auto b = bytes();
            if (b.size() >= 8)
            {
                uint32_t fnLen = 0, dataLen = 0;
                std::memcpy(&fnLen, b.data(), 4);
                std::memcpy(&dataLen, b.data() + 4, 4);
                if (b.size() == 8 + fnLen + dataLen)
                {
                    std::string fname(b.begin() + 8, b.begin() + 8 + fnLen);
                    std::vector<uint8_t> data(b.begin() + 8 + fnLen, b.end());
                    if (OnMediaReceived) OnMediaReceived(fname, data);
                    if (!m_pendingMediaUploadUrl.empty() && m_pendingMediaUploadUrl == "uploading")
                    {
                        m_pendingMediaUploadUrl = "server-media://" + fname;
                        if (OnMediaUploadComplete) OnMediaUploadComplete(m_pendingMediaUploadUrl);
                        m_pendingMediaUploadUrl.clear();
                    }
                }
            }
            break;
        }
        case epyks::PacketType::PFP_RESPONSE:
        {
            epyks::PfpResponse pr;
            if (pr.Deserialize(bytes()))
                if (OnPfpReceived) OnPfpReceived(pr.username, pr.image_data);
            break;
        }

        default: break;
        }
    }
}
