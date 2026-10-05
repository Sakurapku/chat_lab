#include "common/net.hpp"
#include "common/protocol.hpp"

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

std::mutex g_output_mutex;

void print_line(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_output_mutex);
    std::cout << message << std::endl;
}

class UniqueFd {
public:
    UniqueFd() = default;

    explicit UniqueFd(int fd)
        : fd_(fd) {}

    ~UniqueFd() {
        reset();
    }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept
        : fd_(other.release()) {}

    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    int get() const {
        return fd_;
    }

    explicit operator bool() const {
        return fd_ >= 0;
    }

    int release() {
        const int old_fd = fd_;
        fd_ = -1;
        return old_fd;
    }

    void reset(int new_fd = -1) {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = new_fd;
    }

private:
    int fd_ = -1;
};

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }

    std::size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }

    return text.substr(begin, end - begin);
}

bool starts_with(const std::string& text, const std::string& prefix) {
    return text.compare(0, prefix.size(), prefix) == 0;
}

std::uint16_t parse_port(const char* text) {
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);

    if (text == nullptr || *text == '\0' || end == text ||
        *end != '\0' || value < 1 || value > 65535) {
        throw std::invalid_argument(
            "port must be an integer between 1 and 65535");
    }

    return static_cast<std::uint16_t>(value);
}

UniqueFd connect_to_server(const std::string& host,
                           std::uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* raw_addresses = nullptr;
    const std::string port_text = std::to_string(port);

    const int address_result =
        ::getaddrinfo(host.c_str(),
                      port_text.c_str(),
                      &hints,
                      &raw_addresses);

    if (address_result != 0) {
        throw std::runtime_error(
            std::string("getaddrinfo failed: ") +
            gai_strerror(address_result));
    }

    std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> addresses(
        raw_addresses,
        &::freeaddrinfo);

    for (addrinfo* current = raw_addresses;
         current != nullptr;
         current = current->ai_next) {
        UniqueFd socket_fd(::socket(current->ai_family,
                                    current->ai_socktype,
                                    current->ai_protocol));

        if (!socket_fd) {
            continue;
        }

        if (::connect(socket_fd.get(),
                      current->ai_addr,
                      current->ai_addrlen) == 0) {
            return socket_fd;
        }
    }

    throw std::runtime_error("connect failed: unable to connect to server");
}

void send_json_frame(int socket_fd,
                     chat::MessageType type,
                     const nlohmann::json& payload) {
    const chat::Frame frame{type, payload.dump()};
    chat::send_frame(socket_fd, frame);
}

void send_login_request(int socket_fd, const std::string& username) {
    const nlohmann::json payload = {
        {"username", username},
    };

    send_json_frame(socket_fd,
                    chat::MessageType::kLoginRequest,
                    payload);
}

bool wait_for_login_response(int socket_fd) {
    chat::Frame response;

    if (!chat::recv_frame(socket_fd, response)) {
        std::cerr << "服务端在登录过程中关闭了连接\n";
        return false;
    }

    if (response.type == chat::MessageType::kErrorMessage) {
        const nlohmann::json payload =
            nlohmann::json::parse(response.payload);

        std::cerr << "登录失败："
                  << payload.value("message", "unknown error")
                  << '\n';
        return false;
    }

    if (response.type != chat::MessageType::kLoginResponse) {
        std::cerr << "登录失败：服务端返回了意外的消息类型："
                  << chat::to_string(response.type)
                  << '\n';
        return false;
    }

    const nlohmann::json payload =
        nlohmann::json::parse(response.payload);

    const bool ok = payload.value("ok", false);
    const std::string message =
        payload.value("message", "unknown login response");

    if (!ok) {
        std::cerr << "登录失败：" << message << '\n';
        return false;
    }

    print_line("登录成功：" + message);
    return true;
}

void print_help() {
    print_line("");
    print_line("可用命令：");
    print_line("  /help              显示帮助");
    print_line("  /list              查看在线用户");
    print_line("  /all 内容          群发消息");
    print_line("  /msg 用户名 内容   私聊消息");
    print_line("  /quit              退出客户端");
    print_line("  其他输入           默认作为群发消息发送");
    print_line("");
}

void send_chat_request(int socket_fd,
                       const std::string& to,
                       const std::string& content) {
    const nlohmann::json payload = {
        {"to", to},
        {"content", content},
    };

    send_json_frame(socket_fd,
                    chat::MessageType::kSendChatRequest,
                    payload);
}

bool handle_user_input(int socket_fd, const std::string& input) {
    const std::string line = trim(input);

    if (line.empty()) {
        return true;
    }

    if (line == "/quit") {
        return false;
    }

    if (line == "/help") {
        print_help();
        return true;
    }

    if (line == "/list") {
        send_json_frame(socket_fd,
                        chat::MessageType::kListUsersRequest,
                        nlohmann::json::object());
        return true;
    }

    if (starts_with(line, "/all ")) {
        const std::string content = trim(line.substr(5));

        if (content.empty()) {
            print_line("群发内容不能为空");
            return true;
        }

        send_chat_request(socket_fd, "*", content);
        return true;
    }

    if (starts_with(line, "/msg ")) {
        const std::string rest = line.substr(5);
        const std::size_t space_position = rest.find(' ');

        if (space_position == std::string::npos) {
            print_line("用法：/msg 用户名 内容");
            return true;
        }

        const std::string target = trim(rest.substr(0, space_position));
        const std::string content = trim(rest.substr(space_position + 1));

        if (target.empty() || content.empty()) {
            print_line("用法：/msg 用户名 内容");
            return true;
        }

        send_chat_request(socket_fd, target, content);
        return true;
    }

    if (line[0] == '/') {
        print_line("不认识的命令，输入 /help 查看帮助");
        return true;
    }

    // 普通输入默认作为群发消息
    send_chat_request(socket_fd, "*", line);
    return true;
}

void print_user_list(const nlohmann::json& payload) {
    if (!payload.contains("users") || !payload["users"].is_array()) {
        print_line("在线列表格式错误");
        return;
    }

    std::string message = "在线用户：";

    bool first = true;
    for (const auto& user : payload["users"]) {
        if (!user.is_string()) {
            continue;
        }

        if (!first) {
            message += ", ";
        }

        message += user.get<std::string>();
        first = false;
    }

    if (first) {
        message += "（无）";
    }

    print_line(message);
}

void print_chat_message(const nlohmann::json& payload) {
    const std::string from = payload.value("from", "unknown");
    const std::string to = payload.value("to", "unknown");
    const std::string content = payload.value("content", "");

    if (to == "*") {
        print_line("[群发] " + from + ": " + content);
    } else {
        print_line("[私聊] " + from + " -> " + to + ": " + content);
    }
}

void print_error_message(const nlohmann::json& payload) {
    const std::string code = payload.value("code", "UNKNOWN");
    const std::string message = payload.value("message", "unknown error");

    print_line("[错误] " + code + ": " + message);
}

void handle_server_frame(const chat::Frame& frame) {
    const nlohmann::json payload =
        nlohmann::json::parse(frame.payload);

    switch (frame.type) {
        case chat::MessageType::kUserListResponse:
            print_user_list(payload);
            break;

        case chat::MessageType::kChatMessage:
            print_chat_message(payload);
            break;

        case chat::MessageType::kErrorMessage:
            print_error_message(payload);
            break;

        default:
            print_line("收到未知类型的服务端消息：" +
                       std::string(chat::to_string(frame.type)));
            break;
    }
}

void receiver_loop(int socket_fd, std::atomic<bool>& running) {
    while (running.load()) {
        chat::Frame frame;

        try {
            if (!chat::recv_frame(socket_fd, frame)) {
                print_line("服务端关闭了连接，按回车退出");
                running.store(false);
                return;
            }

            handle_server_frame(frame);
        } catch (const chat::ProtocolError& error) {
            print_line(std::string("协议错误：") + error.what());
            running.store(false);
            return;
        } catch (const chat::NetworkError& error) {
            print_line(std::string("网络错误：") + error.what());
            running.store(false);
            return;
        } catch (const std::exception& error) {
            print_line(std::string("处理服务端消息失败：") +
                       error.what());
        }
    }
}

std::string read_username(int argc, char* argv[]) {
    if (argc >= 4) {
        return trim(argv[3]);
    }

    std::cout << "请输入用户名：";
    std::cout.flush();

    std::string username;
    std::getline(std::cin, username);
    return trim(username);
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc > 4) {
            std::cerr << "用法：" << argv[0]
                      << " [主机] [端口] [用户名]\n";
            return 1;
        }

        const std::string host =
            argc >= 2 ? argv[1] : "127.0.0.1";
        const std::uint16_t port =
            argc >= 3 ? parse_port(argv[2]) : 9000;
        const std::string username = read_username(argc, argv);

        if (username.empty()) {
            std::cerr << "用户名不能为空\n";
            return 1;
        }

        UniqueFd socket_fd = connect_to_server(host, port);
        print_line("已连接到 " + host + ":" + std::to_string(port));

        send_login_request(socket_fd.get(), username);

        if (!wait_for_login_response(socket_fd.get())) {
            return 1;
        }

        std::atomic<bool> running{true};
        std::thread receiver(receiver_loop,
                             socket_fd.get(),
                             std::ref(running));

        print_help();

        std::string input;
        while (running.load() && std::getline(std::cin, input)) {
            try {
                if (!handle_user_input(socket_fd.get(), input)) {
                    break;
                }
            } catch (const std::exception& error) {
                print_line(std::string("发送失败：") + error.what());
                break;
            }
        }

        running.store(false);
        ::shutdown(socket_fd.get(), SHUT_RDWR);

        if (receiver.joinable()) {
            receiver.join();
        }

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "client error: " << error.what() << '\n';
        return 1;
    }
}
