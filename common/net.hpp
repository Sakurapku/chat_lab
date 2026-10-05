#ifndef CHAT_COMMON_NET_HPP
#define CHAT_COMMON_NET_HPP

#include <cstddef>
#include <stdexcept>
#include <string>

#include "common/protocol.hpp"

namespace chat {

// 网络错误，例如 send 或 recv 遇到真正的系统错误
class NetworkError : public std::runtime_error {
public:
    explicit NetworkError(const std::string& message)
        : std::runtime_error(message) {}
};

// 把 data 中的所有字节完整发送出去
//
// 如果连接出现错误，抛出 NetworkError。
void send_all(int socket_fd, const std::string& data);

// 从 socket 中完整读取 size 字节
//
// 返回值：
// true  ：成功读取了 size 字节
// false ：读取过程中发现对方关闭了连接
//
// 如果系统调用出现真正的错误，抛出 NetworkError。
bool recv_all(int socket_fd, std::size_t size, std::string& output);

// 把一条 Frame 编码后完整发送出去
void send_frame(int socket_fd, const Frame& frame);

// 从 socket 中读取一条完整 Frame
//
// 返回值：
// true  ：成功读取一条完整消息
// false ：对方关闭了连接
//
// 如果头部不合法，可能抛出 ProtocolError。
// 如果网络出现错误，可能抛出 NetworkError。
bool recv_frame(int socket_fd, Frame& frame);

}  // namespace chat

#endif  // CHAT_COMMON_NET_HPP
