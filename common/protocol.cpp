#include "common/protocol.hpp"

#include <arpa/inet.h>

#include <cstring>

namespace chat {

std::string encode_frame(const Frame& frame) {
    if (frame.payload.size() > kMaxPayloadSize) {
        throw ProtocolError("payload exceeds protocol size limit");
    }

    std::string result;
    result.reserve(kHeaderSize + frame.payload.size());

    // 前两个字节：魔数 CL
    result.push_back(kMagic0);
    result.push_back(kMagic1);

    // 第三个字节：协议版本
    result.push_back(static_cast<char>(kProtocolVersion));

    // 第四个字节：消息类型
    result.push_back(static_cast<char>(frame.type));

    // 第五到第八个字节：负载长度，使用网络大端序
    const std::uint32_t payload_size =
        static_cast<std::uint32_t>(frame.payload.size());
    const std::uint32_t network_payload_size = htonl(payload_size);

    const char* size_bytes =
        reinterpret_cast<const char*>(&network_payload_size);
    result.append(size_bytes, sizeof(network_payload_size));

    // 最后追加 JSON 负载
    result.append(frame.payload);

    return result;
}

Header decode_header(const std::string& header_bytes) {
    if (header_bytes.size() != kHeaderSize) {
        throw ProtocolError("invalid header size");
    }

    if (header_bytes[0] != kMagic0 || header_bytes[1] != kMagic1) {
        throw ProtocolError("invalid magic number");
    }

    const auto version = static_cast<std::uint8_t>(header_bytes[2]);
    if (version != kProtocolVersion) {
        throw ProtocolError("unsupported protocol version");
    }

    const auto type =
        static_cast<MessageType>(static_cast<std::uint8_t>(header_bytes[3]));

    std::uint32_t network_payload_size = 0;
    std::memcpy(&network_payload_size, header_bytes.data() + 4,
                sizeof(network_payload_size));

    const std::uint32_t payload_size = ntohl(network_payload_size);
    if (payload_size > kMaxPayloadSize) {
        throw ProtocolError("payload size exceeds protocol limit");
    }

    return Header{type, payload_size};
}

const char* to_string(MessageType type) {
    switch (type) {
        case MessageType::kLoginRequest:
            return "LOGIN_REQ";
        case MessageType::kListUsersRequest:
            return "LIST_USERS_REQ";
        case MessageType::kSendChatRequest:
            return "SEND_CHAT_REQ";
        case MessageType::kLoginResponse:
            return "LOGIN_RESP";
        case MessageType::kUserListResponse:
            return "USER_LIST_RESP";
        case MessageType::kChatMessage:
            return "CHAT_MSG";
        case MessageType::kErrorMessage:
            return "ERROR_MSG";
    }

    return "UNKNOWN";
}

}  // namespace chat
