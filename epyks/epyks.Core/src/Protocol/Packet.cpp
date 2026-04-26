#include "Packet.h"
#include <cstring>

namespace epyks {

    std::vector<uint8_t> Packet::Serialize() const {
        std::vector<uint8_t> result;
        result.resize(4);
        std::memcpy(result.data(), &type, 4);
        uint32_t dataLen = static_cast<uint32_t>(data.size());
        result.resize(8);
        std::memcpy(result.data() + 4, &dataLen, 4);
        result.insert(result.end(), data.begin(), data.end());
        size_t tsOffset = result.size();
        result.resize(tsOffset + 8);
        std::memcpy(result.data() + tsOffset, &timestamp, 8);
        return result;
    }

    bool Packet::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 16) return false;
        std::memcpy(&type, bytes.data(), 4);
        uint32_t dataLen = 0;
        std::memcpy(&dataLen, bytes.data() + 4, 4);
        if (bytes.size() < 16 + dataLen) return false;
        data.assign(bytes.begin() + 8, bytes.begin() + 8 + dataLen);
        std::memcpy(&timestamp, bytes.data() + 8 + dataLen, 8);
        return true;
    }

    std::vector<uint8_t> Auth::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t uLen = static_cast<uint32_t>(username.size());
        uint32_t pLen = static_cast<uint32_t>(password.size());
        result.resize(8);
        std::memcpy(result.data(), &uLen, 4);
        std::memcpy(result.data() + 4, &pLen, 4);
        result.insert(result.end(), username.begin(), username.end());
        result.insert(result.end(), password.begin(), password.end());
        return result;
    }

    bool Auth::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 8) return false;
        uint32_t uLen = 0, pLen = 0;
        std::memcpy(&uLen, bytes.data(), 4);
        std::memcpy(&pLen, bytes.data() + 4, 4);
        if (bytes.size() != 8 + uLen + pLen) return false;
        username.assign(bytes.begin() + 8, bytes.begin() + 8 + uLen);
        password.assign(bytes.begin() + 8 + uLen, bytes.end());
        return true;
    }

    std::vector<uint8_t> AuthResponse::Serialize() const {
        std::vector<uint8_t> result(9);
        result[0] = success ? 1 : 0;
        std::memcpy(result.data() + 1, &user_id, 8);
        return result;
    }

    bool AuthResponse::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() != 9) return false;
        success = bytes[0] != 0;
        std::memcpy(&user_id, bytes.data() + 1, 8);
        return true;
    }

    std::vector<uint8_t> FriendRequest::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t len = (uint32_t)target_username.size();
        result.resize(4);
        std::memcpy(result.data(), &len, 4);
        result.insert(result.end(), target_username.begin(), target_username.end());
        return result;
    }

    bool FriendRequest::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 4) return false;
        uint32_t len = 0;
        std::memcpy(&len, bytes.data(), 4);
        if (bytes.size() != 4 + len) return false;
        target_username.assign(bytes.begin() + 4, bytes.end());
        return true;
    }

    std::vector<uint8_t> FriendResponse::Serialize() const {
        std::vector<uint8_t> result;
        result.push_back(accepted ? 1 : 0);
        uint32_t len = (uint32_t)target_username.size();
        result.resize(5);
        std::memcpy(result.data() + 1, &len, 4);
        result.insert(result.end(), target_username.begin(), target_username.end());
        return result;
    }

    bool FriendResponse::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 5) return false;
        accepted = bytes[0] != 0;
        uint32_t len = 0;
        std::memcpy(&len, bytes.data() + 1, 4);
        if (bytes.size() != 5 + len) return false;
        target_username.assign(bytes.begin() + 5, bytes.end());
        return true;
    }

    std::vector<uint8_t> PrivateMessage::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t targetLen = (uint32_t)target_username.size();
        uint32_t contentLen = (uint32_t)content.size();
        result.resize(8);
        std::memcpy(result.data(), &targetLen, 4);
        std::memcpy(result.data() + 4, &contentLen, 4);
        result.insert(result.end(), target_username.begin(), target_username.end());
        result.insert(result.end(), content.begin(), content.end());
        return result;
    }

    bool PrivateMessage::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 8) return false;
        uint32_t targetLen = 0, contentLen = 0;
        std::memcpy(&targetLen, bytes.data(), 4);
        std::memcpy(&contentLen, bytes.data() + 4, 4);
        if (bytes.size() != 8 + targetLen + contentLen) return false;
        target_username.assign(bytes.begin() + 8, bytes.begin() + 8 + targetLen);
        content.assign(bytes.begin() + 8 + targetLen, bytes.end());
        return true;
    }

    std::vector<uint8_t> FriendList::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t count = (uint32_t)usernames.size();
        result.resize(4);
        std::memcpy(result.data(), &count, 4);
        for (auto& name : usernames) {
            uint32_t len = (uint32_t)name.size();
            size_t offset = result.size();
            result.resize(offset + 4);
            std::memcpy(result.data() + offset, &len, 4);
            result.insert(result.end(), name.begin(), name.end());
        }
        return result;
    }

    bool FriendList::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 4) return false;
        uint32_t count = 0;
        std::memcpy(&count, bytes.data(), 4);
        size_t pos = 4;
        for (uint32_t i = 0; i < count; i++) {
            if (pos + 4 > bytes.size()) return false;
            uint32_t len = 0;
            std::memcpy(&len, bytes.data() + pos, 4);
            pos += 4;
            if (pos + len > bytes.size()) return false;
            usernames.emplace_back(bytes.begin() + pos, bytes.begin() + pos + len);
            pos += len;
        }
        return pos == bytes.size();
    }

    std::vector<uint8_t> RegisterRequest::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t uLen = (uint32_t)username.size();
        uint32_t pLen = (uint32_t)password.size();
        result.resize(8);
        std::memcpy(result.data(), &uLen, 4);
        std::memcpy(result.data() + 4, &pLen, 4);
        result.insert(result.end(), username.begin(), username.end());
        result.insert(result.end(), password.begin(), password.end());
        return result;
    }

    bool RegisterRequest::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 8) return false;
        uint32_t uLen = 0, pLen = 0;
        std::memcpy(&uLen, bytes.data(), 4);
        std::memcpy(&pLen, bytes.data() + 4, 4);
        if (bytes.size() != 8 + uLen + pLen) return false;
        username.assign(bytes.begin() + 8, bytes.begin() + 8 + uLen);
        password.assign(bytes.begin() + 8 + uLen, bytes.end());
        return true;
    }

    std::vector<uint8_t> RegisterResponse::Serialize() const {
        std::vector<uint8_t> result;
        result.push_back(success ? 1 : 0);
        uint32_t len = (uint32_t)error.size();
        result.resize(5);
        std::memcpy(result.data() + 1, &len, 4);
        result.insert(result.end(), error.begin(), error.end());
        return result;
    }

    bool RegisterResponse::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 5) return false;
        success = bytes[0] != 0;
        uint32_t len = 0;
        std::memcpy(&len, bytes.data() + 1, 4);
        if (bytes.size() != 5 + len) return false;
        error.assign(bytes.begin() + 5, bytes.end());
        return true;
    }

    std::vector<uint8_t> LoginRequest::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t uLen = (uint32_t)username.size();
        uint32_t pLen = (uint32_t)password.size();
        result.resize(8);
        std::memcpy(result.data(), &uLen, 4);
        std::memcpy(result.data() + 4, &pLen, 4);
        result.insert(result.end(), username.begin(), username.end());
        result.insert(result.end(), password.begin(), password.end());
        return result;
    }

    bool LoginRequest::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 8) return false;
        uint32_t uLen = 0, pLen = 0;
        std::memcpy(&uLen, bytes.data(), 4);
        std::memcpy(&pLen, bytes.data() + 4, 4);
        if (bytes.size() != 8 + uLen + pLen) return false;
        username.assign(bytes.begin() + 8, bytes.begin() + 8 + uLen);
        password.assign(bytes.begin() + 8 + uLen, bytes.end());
        return true;
    }

    std::vector<uint8_t> LoginResponse::Serialize() const {
        std::vector<uint8_t> result;
        result.push_back(success ? 1 : 0);
        uint32_t errLen = (uint32_t)error.size();
        uint32_t tokenLen = (uint32_t)session_token.size();

        result.resize(9);
        std::memcpy(result.data() + 1, &errLen, 4);
        std::memcpy(result.data() + 5, &tokenLen, 4);

        result.insert(result.end(), error.begin(), error.end());
        result.insert(result.end(), session_token.begin(), session_token.end());
        return result;
    }

    bool LoginResponse::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 9) return false;
        success = bytes[0] != 0;
        uint32_t errLen = 0, tokenLen = 0;
        std::memcpy(&errLen, bytes.data() + 1, 4);
        std::memcpy(&tokenLen, bytes.data() + 5, 4);

        if (bytes.size() != 9 + errLen + tokenLen) return false;

        error.assign(bytes.begin() + 9, bytes.begin() + 9 + errLen);
        session_token.assign(bytes.begin() + 9 + errLen, bytes.end());
        return true;
    }
    // Servers & Channels
    std::vector<uint8_t> CreateServer::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t len = (uint32_t)server_name.size();
        uint32_t plen = (uint32_t)password.size();
        result.resize(8);
        std::memcpy(result.data(), &len, 4);
        std::memcpy(result.data() + 4, &plen, 4);
        result.insert(result.end(), server_name.begin(), server_name.end());
        result.insert(result.end(), password.begin(), password.end());
        return result;
    }

    bool CreateServer::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 8) return false;
        uint32_t len = 0, plen = 0;
        std::memcpy(&len, bytes.data(), 4);
        std::memcpy(&plen, bytes.data() + 4, 4);
        if (bytes.size() != 8 + len + plen) return false;
        server_name.assign(bytes.begin() + 8, bytes.begin() + 8 + len);
        password.assign(bytes.begin() + 8 + len, bytes.end());
        return true;
    }
  
    std::vector<uint8_t> JoinServer::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t plen = (uint32_t)password.size();
        result.resize(8);
        std::memcpy(result.data(), &server_id, 4);
        std::memcpy(result.data() + 4, &plen, 4);
        result.insert(result.end(), password.begin(), password.end());
        return result;
    }

    bool JoinServer::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 8) return false;
        std::memcpy(&server_id, bytes.data(), 4);
        uint32_t plen = 0;
        std::memcpy(&plen, bytes.data() + 4, 4);
        if (bytes.size() != 8 + plen) return false;
        password.assign(bytes.begin() + 8, bytes.end());
        return true;
	}

    std::vector<uint8_t> LeaveServer::Serialize() const {
        std::vector<uint8_t> result;
        result.resize(4);
        std::memcpy(result.data(), &server_id, 4);
        return result;
	}

    bool LeaveServer::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() != 4) return false;
        std::memcpy(&server_id, bytes.data(), 4);
		return true;
	}

    std::vector<uint8_t> ServerMessage::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t sid = (uint32_t)server_id;
        uint32_t cid = (uint32_t)channel_id;
        uint32_t contentLen = (uint32_t)content.size();
        result.resize(12);
        std::memcpy(result.data(), &sid, 4);
        std::memcpy(result.data() + 4, &cid, 4);
        std::memcpy(result.data() + 8, &contentLen, 4);
        result.insert(result.end(), content.begin(), content.end());
        return result;
    }

    bool ServerMessage::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 12) return false;
        uint32_t sid = 0, cid = 0, contentLen = 0;
        std::memcpy(&sid, bytes.data(), 4);
        std::memcpy(&cid, bytes.data() + 4, 4);
        std::memcpy(&contentLen, bytes.data() + 8, 4);
        if (bytes.size() != 12 + contentLen) return false;
        server_id = sid;
        channel_id = cid;
        content.assign(bytes.begin() + 12, bytes.end());
        return true;
	}

    std::vector<uint8_t> CreateChannel::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t sid = (uint32_t)server_id;
        uint32_t ctype = (uint32_t)type;
        uint32_t len = (uint32_t)channel_name.size();
        result.resize(12);
        std::memcpy(result.data(), &sid, 4);
        std::memcpy(result.data() + 4, &ctype, 4);
        std::memcpy(result.data() + 8, &len, 4);
        result.insert(result.end(), channel_name.begin(), channel_name.end());
        return result;
    }

    bool CreateChannel::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 12) return false;
        uint32_t sid = 0, ctype = 0, len = 0;
        std::memcpy(&sid, bytes.data(), 4);
        std::memcpy(&ctype, bytes.data() + 4, 4);
        std::memcpy(&len, bytes.data() + 8, 4);
        if (bytes.size() != 12 + len) return false;
        server_id = sid;
        type = ctype;
        channel_name.assign(bytes.begin() + 12, bytes.end());
        return true;
    }

    std::vector<uint8_t> DeleteChannel::Serialize() const {
        std::vector<uint8_t> result;
        result.resize(8);
        std::memcpy(result.data(), &server_id, 4);
        std::memcpy(result.data() + 4, &channel_id, 4);
        return result;
    }

    bool DeleteChannel::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() != 8) return false;
        std::memcpy(&server_id, bytes.data(), 4);
        std::memcpy(&channel_id, bytes.data() + 4, 4);
        return true;
    }

    std::vector<uint8_t> KickUser::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t len = (uint32_t)target_username.size();
        result.resize(8);
        std::memcpy(result.data(), &server_id, 4);
        std::memcpy(result.data() + 4, &len, 4);
        result.insert(result.end(), target_username.begin(), target_username.end());
        return result;
    }

    bool KickUser::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 8) return false;
        std::memcpy(&server_id, bytes.data(), 4);
        uint32_t len = 0;
        std::memcpy(&len, bytes.data() + 4, 4);
        if (bytes.size() != 8 + len) return false;
        target_username.assign(bytes.begin() + 8, bytes.end());
        return true;
    }

    std::vector<uint8_t> MuteUser::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t len = (uint32_t)target_username.size();
        result.resize(9);
        std::memcpy(result.data(), &server_id, 4);
        std::memcpy(result.data() + 4, &len, 4);
        result[8] = is_muted ? 1 : 0;
        result.insert(result.end(), target_username.begin(), target_username.end());
        return result;
    }

    bool MuteUser::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 9) return false;
        std::memcpy(&server_id, bytes.data(), 4);
        uint32_t len = 0;
        std::memcpy(&len, bytes.data() + 4, 4);
        if (bytes.size() != 9 + len) return false;
        is_muted = bytes[8] != 0;
        target_username.assign(bytes.begin() + 9, bytes.end());
        return true;
    }

    std::vector<uint8_t> ChannelList::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t len = (uint32_t)data.size();
        result.resize(8);
        std::memcpy(result.data(), &server_id, 4);
        std::memcpy(result.data() + 4, &len, 4);
        result.insert(result.end(), data.begin(), data.end());
        return result;
    }

    bool ChannelList::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 8) return false;
        std::memcpy(&server_id, bytes.data(), 4);
        uint32_t len = 0;
        std::memcpy(&len, bytes.data() + 4, 4);
        if (bytes.size() != 8 + len) return false;
        data.assign(bytes.begin() + 8, bytes.end());
        return true;
    }

    std::vector<uint8_t> JoinVoice::Serialize() const {
        std::vector<uint8_t> result;
        result.resize(8);
        std::memcpy(result.data(), &server_id, 4);
        std::memcpy(result.data() + 4, &channel_id, 4);
        return result;
    }

    bool JoinVoice::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() != 8) return false;
        std::memcpy(&server_id, bytes.data(), 4);
        std::memcpy(&channel_id, bytes.data() + 4, 4);
        return true;
    }

    std::vector<uint8_t> LeaveVoice::Serialize() const {
        std::vector<uint8_t> result;
        result.resize(8);
        std::memcpy(result.data(), &server_id, 4);
        std::memcpy(result.data() + 4, &channel_id, 4);
        return result;
    }

    bool LeaveVoice::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() != 8) return false;
        std::memcpy(&server_id, bytes.data(), 4);
        std::memcpy(&channel_id, bytes.data() + 4, 4);
        return true;
    }

    std::vector<uint8_t> VoiceData::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t nameLen = (uint32_t)username.size();
        uint32_t dataLen = (uint32_t)audio_data.size();
        result.resize(16);
        std::memcpy(result.data(), &nameLen, 4);
        std::memcpy(result.data() + 4, &server_id, 4);
        std::memcpy(result.data() + 8, &channel_id, 4);
        std::memcpy(result.data() + 12, &dataLen, 4);
        result.insert(result.end(), username.begin(), username.end());
        result.insert(result.end(), audio_data.begin(), audio_data.end());
        return result;
    }

    bool VoiceData::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 16) return false;
        uint32_t nameLen = 0, dataLen = 0;
        std::memcpy(&nameLen, bytes.data(), 4);
        std::memcpy(&server_id, bytes.data() + 4, 4);
        std::memcpy(&channel_id, bytes.data() + 8, 4);
        std::memcpy(&dataLen, bytes.data() + 12, 4);
        if (bytes.size() != 16 + nameLen + dataLen) return false;
        username.assign(bytes.begin() + 16, bytes.begin() + 16 + nameLen);
        audio_data.assign(bytes.begin() + 16 + nameLen, bytes.end());
        return true;
    }


    std::vector<uint8_t> TokenLoginRequest::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t uLen = (uint32_t)username.size();
        uint32_t tLen = (uint32_t)token.size();
        result.resize(8);
        std::memcpy(result.data(), &uLen, 4);
        std::memcpy(result.data() + 4, &tLen, 4);
        result.insert(result.end(), username.begin(), username.end());
        result.insert(result.end(), token.begin(), token.end());
        return result;
    }

    bool TokenLoginRequest::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 8) return false;
        uint32_t uLen = 0, tLen = 0;
        std::memcpy(&uLen, bytes.data(), 4);
        std::memcpy(&tLen, bytes.data() + 4, 4);
        if (bytes.size() != 8 + uLen + tLen) return false;
        username.assign(bytes.begin() + 8, bytes.begin() + 8 + uLen);
        token.assign(bytes.begin() + 8 + uLen, bytes.end());
        return true;
    }

} // namespace epyks