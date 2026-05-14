#include "Packet.h"
#include <cstring>

namespace epyks {

    std::vector<uint8_t> Packet::Serialize() const {
        uint32_t dataLen = static_cast<uint32_t>(data.size());
        std::vector<uint8_t> result(16 + dataLen);
        uint8_t* p = result.data();
        uint32_t t = static_cast<uint32_t>(type);
        std::memcpy(p, &t, 4);
        std::memcpy(p + 4, &timestamp, 8);
        std::memcpy(p + 12, &dataLen, 4);
        std::memcpy(p + 16, data.data(), dataLen);
        return result;
    }

    bool Packet::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 16) return false;
        const uint8_t* p = bytes.data();
        uint32_t t;
        std::memcpy(&t, p, 4);
        type = static_cast<PacketType>(t);
        std::memcpy(&timestamp, p + 4, 8);
        uint32_t dataLen;
        std::memcpy(&dataLen, p + 12, 4);
        if (bytes.size() != 16 + dataLen) return false;
        data.assign((const char*)p + 16, dataLen);
        return true;
    }

    std::vector<uint8_t> Auth::Serialize() const {
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

    bool Auth::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 8) return false;
        uint32_t uLen, pLen;
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
        uint32_t len;
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
        uint32_t len;
        std::memcpy(&len, bytes.data() + 1, 4);
        if (bytes.size() != 5 + len) return false;
        target_username.assign(bytes.begin() + 5, bytes.end());
        return true;
    }

    std::vector<uint8_t> PrivateMessage::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t tLen = (uint32_t)target_username.size();
        uint32_t sLen = (uint32_t)sender_username.size();
        uint32_t cLen = (uint32_t)content.size();
        result.resize(28); // 3*4 + 2*8
        std::memcpy(result.data(), &tLen, 4);
        std::memcpy(result.data() + 4, &sLen, 4);
        std::memcpy(result.data() + 8, &cLen, 4);
        std::memcpy(result.data() + 12, &message_id, 8);
        std::memcpy(result.data() + 20, &reply_to_id, 8);
        result.insert(result.end(), target_username.begin(), target_username.end());
        result.insert(result.end(), sender_username.begin(), sender_username.end());
        result.insert(result.end(), content.begin(), content.end());
        return result;
    }

    bool PrivateMessage::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 28) return false;
        uint32_t tLen, sLen, cLen;
        std::memcpy(&tLen, bytes.data(), 4);
        std::memcpy(&sLen, bytes.data() + 4, 4);
        std::memcpy(&cLen, bytes.data() + 8, 4);
        std::memcpy(&message_id, bytes.data() + 12, 8);
        std::memcpy(&reply_to_id, bytes.data() + 20, 8);
        if (bytes.size() != 28 + tLen + sLen + cLen) return false;
        target_username.assign(bytes.begin() + 28, bytes.begin() + 28 + tLen);
        sender_username.assign(bytes.begin() + 28 + tLen, bytes.begin() + 28 + tLen + sLen);
        content.assign(bytes.begin() + 28 + tLen + sLen, bytes.end());
        return true;
    }

    std::vector<uint8_t> FriendList::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t count = (uint32_t)usernames.size();
        result.resize(4);
        std::memcpy(result.data(), &count, 4);
        for (const auto& u : usernames) {
            uint32_t len = (uint32_t)u.size();
            uint32_t start = (uint32_t)result.size();
            result.resize(start + 4);
            std::memcpy(result.data() + start, &len, 4);
            result.insert(result.end(), u.begin(), u.end());
        }
        return result;
    }

    bool FriendList::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 4) return false;
        uint32_t count;
        std::memcpy(&count, bytes.data(), 4);
        size_t offset = 4;
        usernames.clear();
        for (uint32_t i = 0; i < count; ++i) {
            if (offset + 4 > bytes.size()) return false;
            uint32_t len;
            std::memcpy(&len, bytes.data() + offset, 4);
            offset += 4;
            if (offset + len > bytes.size()) return false;
            usernames.emplace_back(bytes.begin() + offset, bytes.begin() + offset + len);
            offset += len;
        }
        return true;
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
        uint32_t uLen, pLen;
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
        uint32_t eLen = (uint32_t)error.size();
        result.resize(5);
        std::memcpy(result.data() + 1, &eLen, 4);
        result.insert(result.end(), error.begin(), error.end());
        return result;
    }

    bool RegisterResponse::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 5) return false;
        success = bytes[0] != 0;
        uint32_t eLen;
        std::memcpy(&eLen, bytes.data() + 1, 4);
        if (bytes.size() != 5 + eLen) return false;
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
        uint32_t uLen, pLen;
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
        uint32_t eLen = (uint32_t)error.size();
        uint32_t tLen = (uint32_t)session_token.size();
        result.resize(9);
        std::memcpy(result.data() + 1, &eLen, 4);
        std::memcpy(result.data() + 5, &tLen, 4);
        result.insert(result.end(), error.begin(), error.end());
        result.insert(result.end(), session_token.begin(), session_token.end());
        return result;
    }

    bool LoginResponse::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 9) return false;
        success = bytes[0] != 0;
        uint32_t eLen, tLen;
        std::memcpy(&eLen, bytes.data() + 1, 4);
        std::memcpy(&tLen, bytes.data() + 5, 4);
        if (bytes.size() != 9 + eLen + tLen) return false;
        error.assign(bytes.begin() + 9, bytes.begin() + 9 + eLen);
        session_token.assign(bytes.begin() + 9 + eLen, bytes.end());
        return true;
    }

    std::vector<uint8_t> CreateServer::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t nLen = (uint32_t)server_name.size();
        uint32_t pLen = (uint32_t)password.size();
        result.resize(8);
        std::memcpy(result.data(), &nLen, 4);
        std::memcpy(result.data() + 4, &pLen, 4);
        result.insert(result.end(), server_name.begin(), server_name.end());
        result.insert(result.end(), password.begin(), password.end());
        return result;
    }

    bool CreateServer::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 8) return false;
        uint32_t nLen, pLen;
        std::memcpy(&nLen, bytes.data(), 4);
        std::memcpy(&pLen, bytes.data() + 4, 4);
        if (bytes.size() != 8 + nLen + pLen) return false;
        server_name.assign(bytes.begin() + 8, bytes.begin() + 8 + nLen);
        password.assign(bytes.begin() + 8 + nLen, bytes.end());
        return true;
    }

    std::vector<uint8_t> JoinServer::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t pLen = (uint32_t)password.size();
        result.resize(8);
        std::memcpy(result.data(), &server_id, 4);
        std::memcpy(result.data() + 4, &pLen, 4);
        result.insert(result.end(), password.begin(), password.end());
        return result;
    }

    bool JoinServer::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 8) return false;
        uint32_t pLen;
        std::memcpy(&server_id, bytes.data(), 4);
        std::memcpy(&pLen, bytes.data() + 4, 4);
        if (bytes.size() != 8 + pLen) return false;
        password.assign(bytes.begin() + 8, bytes.end());
        return true;
    }

    std::vector<uint8_t> LeaveServer::Serialize() const {
        std::vector<uint8_t> result(4);
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
        uint32_t uLen = (uint32_t)username.size();
        uint32_t cLen = (uint32_t)content.size();
        result.resize(32); // 4*4 + 2*8 = 32
        std::memcpy(result.data(), &server_id, 4);
        std::memcpy(result.data() + 4, &channel_id, 4);
        std::memcpy(result.data() + 8, &uLen, 4);
        std::memcpy(result.data() + 12, &cLen, 4);
        std::memcpy(result.data() + 16, &message_id, 8);
        std::memcpy(result.data() + 24, &reply_to_id, 8);

        result.insert(result.end(), username.begin(), username.end());
        result.insert(result.end(), content.begin(), content.end());
        return result;
    }

    bool ServerMessage::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 32) return false;
        uint32_t uLen, cLen;
        std::memcpy(&server_id, bytes.data(), 4);
        std::memcpy(&channel_id, bytes.data() + 4, 4);
        std::memcpy(&uLen, bytes.data() + 8, 4);
        std::memcpy(&cLen, bytes.data() + 12, 4);
        std::memcpy(&message_id, bytes.data() + 16, 8);
        std::memcpy(&reply_to_id, bytes.data() + 24, 8);
        if (bytes.size() != 32 + uLen + cLen) return false;
        username.assign(bytes.begin() + 32, bytes.begin() + 32 + uLen);
        content.assign(bytes.begin() + 32 + uLen, bytes.end());
        return true;
    }

    std::vector<uint8_t> DeleteMessage::Serialize() const {
        std::vector<uint8_t> result(16);
        std::memcpy(result.data(), &server_id, 4);
        std::memcpy(result.data() + 4, &channel_id, 4);
        std::memcpy(result.data() + 8, &message_id, 8);
        return result;
    }

    bool DeleteMessage::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() != 16) return false;
        std::memcpy(&server_id, bytes.data(), 4);
        std::memcpy(&channel_id, bytes.data() + 4, 4);
        std::memcpy(&message_id, bytes.data() + 8, 8);
        return true;
    }

    std::vector<uint8_t> EditMessage::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t cLen = (uint32_t)new_content.size();
        result.resize(20);
        std::memcpy(result.data(), &server_id, 4);
        std::memcpy(result.data() + 4, &channel_id, 4);
        std::memcpy(result.data() + 8, &message_id, 8);
        std::memcpy(result.data() + 16, &cLen, 4);
        result.insert(result.end(), new_content.begin(), new_content.end());
        return result;
    }

    bool EditMessage::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 20) return false;
        uint32_t cLen;
        std::memcpy(&server_id, bytes.data(), 4);
        std::memcpy(&channel_id, bytes.data() + 4, 4);
        std::memcpy(&message_id, bytes.data() + 8, 8);
        std::memcpy(&cLen, bytes.data() + 16, 4);
        if (bytes.size() != 20 + cLen) return false;
        new_content.assign(bytes.begin() + 20, bytes.end());
        return true;
    }

    std::vector<uint8_t> CreateChannel::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t nLen = (uint32_t)channel_name.size();
        uint32_t cLen = (uint32_t)category.size();
        result.resize(16);
        std::memcpy(result.data(), &server_id, 4);
        std::memcpy(result.data() + 4, &type, 4);
        std::memcpy(result.data() + 8, &nLen, 4);
        std::memcpy(result.data() + 12, &cLen, 4);
        result.insert(result.end(), channel_name.begin(), channel_name.end());
        result.insert(result.end(), category.begin(), category.end());
        return result;
    }

    bool CreateChannel::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 16) return false;
        uint32_t nLen, cLen;
        std::memcpy(&server_id, bytes.data(), 4);
        std::memcpy(&type, bytes.data() + 4, 4);
        std::memcpy(&nLen, bytes.data() + 8, 4);
        std::memcpy(&cLen, bytes.data() + 12, 4);
        if (bytes.size() != 16 + nLen + cLen) return false;
        channel_name.assign(bytes.begin() + 16, bytes.begin() + 16 + nLen);
        category.assign(bytes.begin() + 16 + nLen, bytes.end());
        return true;
    }

    std::vector<uint8_t> EditChannel::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t nLen = (uint32_t)name.size();
        uint32_t cLen = (uint32_t)category.size();
        result.resize(20);
        std::memcpy(result.data(), &server_id, 4);
        std::memcpy(result.data() + 4, &channel_id, 4);
        std::memcpy(result.data() + 8, &type, 4);
        std::memcpy(result.data() + 12, &nLen, 4);
        std::memcpy(result.data() + 16, &cLen, 4);
        result.insert(result.end(), name.begin(), name.end());
        result.insert(result.end(), category.begin(), category.end());
        return result;
    }

    bool EditChannel::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 20) return false;
        uint32_t nLen, cLen;
        std::memcpy(&server_id, bytes.data(), 4);
        std::memcpy(&channel_id, bytes.data() + 4, 4);
        std::memcpy(&type, bytes.data() + 8, 4);
        std::memcpy(&nLen, bytes.data() + 12, 4);
        std::memcpy(&cLen, bytes.data() + 16, 4);
        if (bytes.size() != 20 + nLen + cLen) return false;
        name.assign(bytes.begin() + 20, bytes.begin() + 20 + nLen);
        category.assign(bytes.begin() + 20 + nLen, bytes.end());
        return true;
    }

    std::vector<uint8_t> DeleteChannel::Serialize() const {
        std::vector<uint8_t> result(8);
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
        uint32_t len;
        std::memcpy(&server_id, bytes.data(), 4);
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
        uint32_t len;
        std::memcpy(&server_id, bytes.data(), 4);
        std::memcpy(&len, bytes.data() + 4, 4);
        is_muted = bytes[8] != 0;
        if (bytes.size() != 9 + len) return false;
        target_username.assign(bytes.begin() + 9, bytes.end());
        return true;
    }

    std::vector<uint8_t> ChannelList::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t dLen = (uint32_t)data.size();
        result.resize(8);
        std::memcpy(result.data(), &server_id, 4);
        std::memcpy(result.data() + 4, &dLen, 4);
        result.insert(result.end(), data.begin(), data.end());
        return result;
    }

    bool ChannelList::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 8) return false;
        uint32_t dLen;
        std::memcpy(&server_id, bytes.data(), 4);
        std::memcpy(&dLen, bytes.data() + 4, 4);
        if (bytes.size() != 8 + dLen) return false;
        data.assign(bytes.begin() + 8, bytes.end());
        return true;
    }

    std::vector<uint8_t> JoinVoice::Serialize() const {
        std::vector<uint8_t> result(8);
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
        std::vector<uint8_t> result(8);
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
        uint32_t nLen = (uint32_t)username.size();
        uint32_t dLen = (uint32_t)audio_data.size();
        result.resize(16);
        std::memcpy(result.data(), &nLen, 4);
        std::memcpy(result.data() + 4, &dLen, 4);
        std::memcpy(result.data() + 8, &server_id, 4);
        std::memcpy(result.data() + 12, &channel_id, 4);
        result.insert(result.end(), username.begin(), username.end());
        result.insert(result.end(), audio_data.begin(), audio_data.end());
        return result;
    }

    bool VoiceData::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 16) return false;
        uint32_t nLen, dLen;
        std::memcpy(&nLen, bytes.data(), 4);
        std::memcpy(&dLen, bytes.data() + 4, 4);
        std::memcpy(&server_id, bytes.data() + 8, 4);
        std::memcpy(&channel_id, bytes.data() + 12, 4);
        if (bytes.size() != 16 + nLen + dLen) return false;
        username.assign(bytes.begin() + 16, bytes.begin() + 16 + nLen);
        audio_data.assign(bytes.begin() + 16 + nLen, bytes.end());
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
        uint32_t uLen, tLen;
        std::memcpy(&uLen, bytes.data(), 4);
        std::memcpy(&tLen, bytes.data() + 4, 4);
        if (bytes.size() != 8 + uLen + tLen) return false;
        username.assign(bytes.begin() + 8, bytes.begin() + 8 + uLen);
        token.assign(bytes.begin() + 8 + uLen, bytes.end());
        return true;
    }

    std::vector<uint8_t> UserProfile::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t uLen = (uint32_t)username.size();
        uint32_t bLen = (uint32_t)bio.size();
        uint32_t pLen = (uint32_t)pfp_url.size();
        result.resize(12);
        std::memcpy(result.data(), &uLen, 4);
        std::memcpy(result.data() + 4, &bLen, 4);
        std::memcpy(result.data() + 8, &pLen, 4);
        result.insert(result.end(), username.begin(), username.end());
        result.insert(result.end(), bio.begin(), bio.end());
        result.insert(result.end(), pfp_url.begin(), pfp_url.end());
        return result;
    }

    bool UserProfile::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 12) return false;
        uint32_t uLen, bLen, pLen;
        std::memcpy(&uLen, bytes.data(), 4);
        std::memcpy(&bLen, bytes.data() + 4, 4);
        std::memcpy(&pLen, bytes.data() + 8, 4);
        if (bytes.size() != 12 + uLen + bLen + pLen) return false;
        username.assign(bytes.begin() + 12, bytes.begin() + 12 + uLen);
        bio.assign(bytes.begin() + 12 + uLen, bytes.begin() + 12 + uLen + bLen);
        pfp_url.assign(bytes.begin() + 12 + uLen + bLen, bytes.end());
        return true;
    }

    std::vector<uint8_t> ProfileUpdate::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t bLen = (uint32_t)bio.size();
        uint32_t pLen = (uint32_t)pfp_url.size();
        result.resize(8);
        std::memcpy(result.data(), &bLen, 4);
        std::memcpy(result.data() + 4, &pLen, 4);
        result.insert(result.end(), bio.begin(), bio.end());
        result.insert(result.end(), pfp_url.begin(), pfp_url.end());
        return result;
    }

    bool ProfileUpdate::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 8) return false;
        uint32_t bLen, pLen;
        std::memcpy(&bLen, bytes.data(), 4);
        std::memcpy(&pLen, bytes.data() + 4, 4);
        if (bytes.size() != 8 + bLen + pLen) return false;
        bio.assign(bytes.begin() + 8, bytes.begin() + 8 + bLen);
        pfp_url.assign(bytes.begin() + 8 + bLen, bytes.end());
        return true;
    }

    std::vector<uint8_t> MemberListRequest::Serialize() const {
        std::vector<uint8_t> result(4);
        std::memcpy(result.data(), &server_id, 4);
        return result;
    }

    bool MemberListRequest::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() != 4) return false;
        std::memcpy(&server_id, bytes.data(), 4);
        return true;
    }

    std::vector<uint8_t> MemberListResponse::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t count = (uint32_t)members.size();
        result.resize(8);
        std::memcpy(result.data(), &server_id, 4);
        std::memcpy(result.data() + 4, &count, 4);
        for (const auto& m : members) {
            uint32_t uLen = (uint32_t)m.username.size();
            uint32_t bLen = (uint32_t)m.bio.size();
            uint32_t pLen = (uint32_t)m.pfp_url.size();
            uint32_t meta4[5] = { uLen, bLen, pLen, (uint32_t)m.role, (uint32_t)m.voice_channel_id };
            uint8_t meta1[2] = { (uint8_t)(m.is_muted ? 1 : 0), (uint8_t)(m.is_talking ? 1 : 0) };
            size_t start = result.size();
            result.resize(start + 22);
            std::memcpy(result.data() + start, meta4, 20);
            std::memcpy(result.data() + start + 20, &meta1[0], 1);
            std::memcpy(result.data() + start + 21, &meta1[1], 1);
            result.insert(result.end(), m.username.begin(), m.username.end());
            result.insert(result.end(), m.bio.begin(), m.bio.end());
            result.insert(result.end(), m.pfp_url.begin(), m.pfp_url.end());
        }
        return result;
    }

    bool MemberListResponse::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 8) return false;
        std::memcpy(&server_id, bytes.data(), 4);
        uint32_t count = 0;
        std::memcpy(&count, bytes.data() + 4, 4);
        size_t offset = 8;
        members.clear();
        for (uint32_t i = 0; i < count; ++i) {
            if (offset + 22 > bytes.size()) return false;
            uint32_t meta4[5];
            std::memcpy(meta4, bytes.data() + offset, 20);
            uint8_t muted = bytes[offset + 20];
            uint8_t talking = bytes[offset + 21];
            offset += 22;
            if (offset + meta4[0] + meta4[1] + meta4[2] > bytes.size()) return false;
            MemberInfo m;
            m.username.assign(bytes.begin() + offset, bytes.begin() + offset + meta4[0]);
            offset += meta4[0];
            m.bio.assign(bytes.begin() + offset, bytes.begin() + offset + meta4[1]);
            offset += meta4[1];
            m.pfp_url.assign(bytes.begin() + offset, bytes.begin() + offset + meta4[2]);
            offset += meta4[2];
            m.role = (int)meta4[3];
            m.voice_channel_id = (int)meta4[4];
            m.is_muted = muted != 0;
            m.is_talking = talking != 0;
            members.push_back(m);
        }
        return true;
    }

    std::vector<uint8_t> PfpUpload::Serialize() const {
        std::vector<uint8_t> out;
        uint32_t len = static_cast<uint32_t>(image_data.size());
        out.resize(4);
        std::memcpy(out.data(), &len, 4);
        out.insert(out.end(), image_data.begin(), image_data.end());
        return out;
    }

    bool PfpUpload::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 4) return false;
        uint32_t len = 0;
        std::memcpy(&len, bytes.data(), 4);
        if (bytes.size() != 4 + len) return false;
        image_data.assign(bytes.begin() + 4, bytes.end());
        return true;
    }

    std::vector<uint8_t> PfpRequest::Serialize() const {
        std::vector<uint8_t> out;
        uint32_t len = static_cast<uint32_t>(username.size());
        out.resize(4);
        std::memcpy(out.data(), &len, 4);
        out.insert(out.end(), username.begin(), username.end());
        return out;
    }

    bool PfpRequest::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 4) return false;
        uint32_t len = 0;
        std::memcpy(&len, bytes.data(), 4);
        if (bytes.size() != 4 + len) return false;
        username.assign(bytes.begin() + 4, bytes.end());
        return true;
    }

    std::vector<uint8_t> PfpResponse::Serialize() const {
        std::vector<uint8_t> out;
        uint32_t uLen = static_cast<uint32_t>(username.size());
        uint32_t dLen = static_cast<uint32_t>(image_data.size());
        out.resize(8);
        std::memcpy(out.data(), &uLen, 4);
        std::memcpy(out.data() + 4, &dLen, 4);
        out.insert(out.end(), username.begin(), username.end());
        out.insert(out.end(), image_data.begin(), image_data.end());
        return out;
    }

    bool PfpResponse::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 8) return false;
        uint32_t uLen = 0, dLen = 0;
        std::memcpy(&uLen, bytes.data(), 4);
        std::memcpy(&dLen, bytes.data() + 4, 4);
        if (bytes.size() != 8 + uLen + dLen) return false;
        username.assign(bytes.begin() + 8, bytes.begin() + 8 + uLen);
        image_data.assign(bytes.begin() + 8 + uLen, bytes.end());
        return true;
    }

    std::vector<uint8_t> MediaRequest::Serialize() const {
        std::vector<uint8_t> out;
        uint32_t len = static_cast<uint32_t>(filename.size());
        out.resize(4);
        std::memcpy(out.data(), &len, 4);
        out.insert(out.end(), filename.begin(), filename.end());
        return out;
    }

    bool MediaRequest::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 4) return false;
        uint32_t len = 0;
        std::memcpy(&len, bytes.data(), 4);
        if (bytes.size() != 4 + len) return false;
        filename.assign(bytes.begin() + 4, bytes.end());
        return true;
    }

    std::vector<uint8_t> MediaUpload::Serialize() const {
        std::vector<uint8_t> out;
        uint32_t len = static_cast<uint32_t>(media_data.size());
        out.resize(4);
        std::memcpy(out.data(), &len, 4);
        out.insert(out.end(), media_data.begin(), media_data.end());
        return out;
    }

    bool MediaUpload::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 4) return false;
        uint32_t len = 0;
        std::memcpy(&len, bytes.data(), 4);
        if (bytes.size() != 4 + len) return false;
        media_data.assign(bytes.begin() + 4, bytes.end());
        return true;
    }

    std::vector<uint8_t> MediaResponse::Serialize() const {
        std::vector<uint8_t> out;
        uint32_t len = static_cast<uint32_t>(url.size());
        out.resize(4);
        std::memcpy(out.data(), &len, 4);
        out.insert(out.end(), url.begin(), url.end());
        return out;
    }

    bool MediaResponse::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 4) return false;
        uint32_t len = 0;
        std::memcpy(&len, bytes.data(), 4);
        if (bytes.size() != 4 + len) return false;
        url.assign(bytes.begin() + 4, bytes.end());
        return true;
    }

    std::vector<uint8_t> DeleteAccount::Serialize() const {
        std::vector<uint8_t> out;
        uint32_t len = static_cast<uint32_t>(password.size());
        out.resize(4);
        std::memcpy(out.data(), &len, 4);
        out.insert(out.end(), password.begin(), password.end());
        return out;
    }

    bool DeleteAccount::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 4) return false;
        uint32_t len = 0;
        std::memcpy(&len, bytes.data(), 4);
        if (bytes.size() != 4 + len) return false;
        password.assign(bytes.begin() + 4, bytes.end());
        return true;
    }

    std::vector<uint8_t> Unfriend::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t len = (uint32_t)target_username.size();
        result.resize(4);
        std::memcpy(result.data(), &len, 4);
        result.insert(result.end(), target_username.begin(), target_username.end());
        return result;
    }

    bool Unfriend::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 4) return false;
        uint32_t len;
        std::memcpy(&len, bytes.data(), 4);
        if (bytes.size() != 4 + len) return false;
        target_username.assign(bytes.begin() + 4, bytes.end());
        return true;
    }

    std::vector<uint8_t> RenameServer::Serialize() const {
        std::vector<uint8_t> result(4);
        std::memcpy(result.data(), &server_id, 4);
        uint32_t len = (uint32_t)new_name.size();
        result.resize(8);
        std::memcpy(result.data() + 4, &len, 4);
        result.insert(result.end(), new_name.begin(), new_name.end());
        return result;
    }

    bool RenameServer::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 8) return false;
        std::memcpy(&server_id, bytes.data(), 4);
        uint32_t len;
        std::memcpy(&len, bytes.data() + 4, 4);
        if (bytes.size() != 8 + len) return false;
        new_name.assign(bytes.begin() + 8, bytes.end());
        return true;
    }

    std::vector<uint8_t> GetProfile::Serialize() const {
        std::vector<uint8_t> result;
        uint32_t len = (uint32_t)username.size();
        result.resize(4);
        std::memcpy(result.data(), &len, 4);
        result.insert(result.end(), username.begin(), username.end());
        return result;
    }

    bool GetProfile::Deserialize(const std::vector<uint8_t>& bytes) {
        if (bytes.size() < 4) return false;
        uint32_t len;
        std::memcpy(&len, bytes.data(), 4);
        if (bytes.size() != 4 + len) return false;
        username.assign(bytes.begin() + 4, bytes.end());
        return true;
    }

} // namespace epyks