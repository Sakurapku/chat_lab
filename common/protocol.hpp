#ifndef CHAT_COMMON_PROTOCOL_HPP
#define CHAT_COMMON_PROTOCOL_HPP

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace chat {

// 协议魔数：前两个字节必须分别是 'C' 和 'L'
inline constexpr char kMagic0 = 'C';
inline constexpr char kMagic1 = 'L';

// 当前协议版本
inline constexpr std::uint8_t kProtocolVersion = 1;

// 头部固定为 8 字节
inline constexpr std::size_t kHeaderSize = 8;

// 协议层上限：单个 JSON 负载最大 4 MiB
inline constexpr std::uint32_t kMaxPayloadSize = 4U * 1024U * 1024U;

// 业务层上限：单条聊天内容最大 1 MiB
inline constexpr std::size_t kMaxChatContentSize = 1024U * 1024U;

// 消息类型
enum class MessageType : std::uint8_t {
    // 客户端发给服务端
    kLoginRequest = 1,
    kListUsersRequest = 2,
    kSendChatRequest = 3,

    // 服务端发给客户端
    kLoginResponse = 101,
    kUserListResponse = 102,
    kChatMessage = 103,
    kErrorMessage = 104,
};

// 一条完整消息：
// type 表示消息类型，payload 表示 JSON 字符串
struct Frame {
    MessageType type = MessageType::kLoginRequest;
    std::string payload;
};

// 解析出来的 8 字节头部
struct Header {
    MessageType type;
    std::uint32_t payload_size;
};

// 协议错误，例如魔数错误、版本错误、长度超过上限
class ProtocolError : public std::runtime_error {
public:
    explicit ProtocolError(const std::string& message)
        : std::runtime_error(message) {}
};

// 把一条消息打包成：8 字节头部 + JSON 负载
std::string encode_frame(const Frame& frame);

// 解析固定 8 字节头部，返回消息类型和负载长度
Header decode_header(const std::string& header_bytes);

// 把消息类型转换成便于打印的文字
const char* to_string(MessageType type);

}  // namespace chat

#endif  // CHAT_COMMON_PROTOCOL_HPP
