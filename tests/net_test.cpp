#include "common/net.hpp"
#include "common/protocol.hpp"

#include <cerrno>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {

int g_failures = 0;

void check(bool condition, const std::string& test_name) {
    if (condition) {
        std::cout << "[PASS] " << test_name << '\n';
    } else {
        std::cerr << "[FAIL] " << test_name << '\n';
        ++g_failures;
    }
}

class SocketPair {
public:
    SocketPair() {
        int fds[2] = {-1, -1};

        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
            throw std::runtime_error(
                std::string("socketpair failed: ") +
                std::strerror(errno));
        }

        first_ = fds[0];
        second_ = fds[1];
    }

    ~SocketPair() {
        if (first_ >= 0) {
            ::close(first_);
        }

        if (second_ >= 0) {
            ::close(second_);
        }
    }

    SocketPair(const SocketPair&) = delete;
    SocketPair& operator=(const SocketPair&) = delete;

    int first() const {
        return first_;
    }

    int second() const {
        return second_;
    }

private:
    int first_ = -1;
    int second_ = -1;
};

void test_send_and_recv_frame() {
    SocketPair sockets;

    const chat::Frame sent{
        chat::MessageType::kLoginRequest,
        R"({"username":"alice"})",
    };

    chat::send_frame(sockets.first(), sent);

    chat::Frame received;
    check(chat::recv_frame(sockets.second(), received),
          "recv_frame 成功读取完整消息");

    check(received.type == sent.type,
          "接收到的消息类型正确");

    check(received.payload == sent.payload,
          "接收到的 JSON 负载正确");
}

void test_empty_payload() {
    SocketPair sockets;

    const chat::Frame sent{
        chat::MessageType::kListUsersRequest,
        "",
    };

    chat::send_frame(sockets.first(), sent);

    chat::Frame received;
    check(chat::recv_frame(sockets.second(), received),
          "成功读取空负载消息");

    check(received.type == chat::MessageType::kListUsersRequest,
          "空负载消息的类型正确");

    check(received.payload.empty(),
          "空负载消息的 payload 为空");
}

void test_split_frame() {
    SocketPair sockets;

    const chat::Frame sent{
        chat::MessageType::kSendChatRequest,
        R"({"to":"*","content":"hello"})",
    };

    const std::string bytes = chat::encode_frame(sent);

    // 故意把一条完整消息拆成三次发送
    chat::send_all(sockets.first(), bytes.substr(0, 3));
    chat::send_all(sockets.first(), bytes.substr(3, 4));
    chat::send_all(sockets.first(), bytes.substr(7));

    chat::Frame received;
    check(chat::recv_frame(sockets.second(), received),
          "分段发送的消息仍能被完整读取");

    check(received.type == sent.type,
          "分段发送后的消息类型正确");

    check(received.payload == sent.payload,
          "分段发送后的 JSON 负载正确");
}

void test_two_frames_together() {
    SocketPair sockets;

    const chat::Frame first{
        chat::MessageType::kListUsersRequest,
        "{}",
    };

    const chat::Frame second{
        chat::MessageType::kSendChatRequest,
        R"({"to":"bob","content":"hello"})",
    };

    const std::string bytes =
        chat::encode_frame(first) + chat::encode_frame(second);

    // 故意把两条消息合并成一次发送
    chat::send_all(sockets.first(), bytes);

    chat::Frame received_first;
    chat::Frame received_second;

    check(chat::recv_frame(sockets.second(), received_first),
          "成功读取两条消息中的第一条");

    check(chat::recv_frame(sockets.second(), received_second),
          "成功读取两条消息中的第二条");

    check(received_first.type == first.type,
          "第一条消息类型正确");

    check(received_first.payload == first.payload,
          "第一条消息负载正确");

    check(received_second.type == second.type,
          "第二条消息类型正确");

    check(received_second.payload == second.payload,
          "第二条消息负载正确");
}

void test_peer_close() {
    SocketPair sockets;

    const int shutdown_result = ::shutdown(sockets.first(), SHUT_RDWR);
    check(shutdown_result == 0, "成功关闭 socketpair 的一端");

    chat::Frame received;
    check(!chat::recv_frame(sockets.second(), received),
          "对端关闭后 recv_frame 返回 false");
}

}  // namespace

int main() {
    try {
        test_send_and_recv_frame();
        test_empty_payload();
        test_split_frame();
        test_two_frames_together();
        test_peer_close();
    } catch (const std::exception& error) {
        std::cerr << "测试过程中出现异常：" << error.what() << '\n';
        return 1;
    }

    if (g_failures != 0) {
        std::cerr << g_failures << " 个网络测试失败\n";
        return 1;
    }

    std::cout << "所有网络收发测试通过\n";
    return 0;
}
