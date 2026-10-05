#include "common/net.hpp"
#include "common/protocol.hpp"
#include "server/client_connection.hpp"
#include "server/user_table.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

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
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

std::mutex g_log_mutex;
UserTable g_users;

void log_line(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    std::cout << message << std::endl;
}

std::string errno_message(const char* operation) {
    const int error_number = errno;
    return std::string(operation) + " failed: " +
           std::strerror(error_number);
}

class LoginGuard {
public:
    LoginGuard(UserTable& users, int socket_fd)
        : users_(users), socket_fd_(socket_fd) {}

    ~LoginGuard() {
        if (!logged_in_) {
            return;
        }

        const std::optional<std::string> removed =
            users_.remove_by_fd(socket_fd_);

        if (removed.has_value()) {
            log_line("user disconnected: " + removed.value());
        }
    }

    LoginGuard(const LoginGuard&) = delete;
    LoginGuard& operator=(const LoginGuard&) = delete;

    void mark_logged_in() {
        logged_in_ = true;
    }

    bool is_logged_in() const {
        return logged_in_;
    }

private:
    UserTable& users_;
    int socket_fd_;
    bool logged_in_ = false;
};

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

UniqueFd create_listener(std::uint16_t port) {
    UniqueFd listener(::socket(AF_INET, SOCK_STREAM, 0));
    if (!listener) {
        throw std::runtime_error(errno_message("socket"));
    }

    const int reuse = 1;
    if (::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR,
                     &reuse, sizeof(reuse)) != 0) {
        throw std::runtime_error(errno_message("setsockopt"));
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (::bind(listener.get(),
               reinterpret_cast<sockaddr*>(&address),
               sizeof(address)) != 0) {
        throw std::runtime_error(errno_message("bind"));
    }

    if (::listen(listener.get(), SOMAXCONN) != 0) {
        throw std::runtime_error(errno_message("listen"));
    }

    return listener;
}

void send_frame_to_client(
    const std::shared_ptr<ClientConnection>& client,
    const chat::Frame& frame) {
    std::lock_guard<std::mutex> lock(client->send_mutex);
    chat::send_frame(client->fd(), frame);
}

void send_json_frame(
    const std::shared_ptr<ClientConnection>& client,
    chat::MessageType type,
    const nlohmann::json& payload) {
    const chat::Frame frame{type, payload.dump()};
    send_frame_to_client(client, frame);
}

void send_error(
    const std::shared_ptr<ClientConnection>& client,
    const std::string& code,
    const std::string& message) {
    try {
        const nlohmann::json payload = {
            {"code", code},
            {"message", message},
        };

        send_json_frame(client,
                        chat::MessageType::kErrorMessage,
                        payload);
    } catch (const std::exception&) {
        // 当前连接可能已经损坏，发送错误失败时不再继续抛出异常。
    }
}

void send_login_response(
    const std::shared_ptr<ClientConnection>& client,
    bool ok,
    const std::string& message) {
    const nlohmann::json payload = {
        {"ok", ok},
        {"message", message},
    };

    send_json_frame(client,
                    chat::MessageType::kLoginResponse,
                    payload);
}

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

bool is_valid_username(const std::string& username) {
    if (username.empty() || username.size() > 32) {
        return false;
    }

    if (username.find('\n') != std::string::npos ||
        username.find('\r') != std::string::npos) {
        return false;
    }

    return true;
}

bool handle_login(
    const std::shared_ptr<ClientConnection>& client,
    const chat::Frame& frame,
    LoginGuard& login_guard) {
    nlohmann::json payload;

    try {
        payload = nlohmann::json::parse(frame.payload);
    } catch (const nlohmann::json::exception&) {
        send_error(client, "BAD_REQUEST", "invalid JSON payload");
        return false;
    }

    if (!payload.is_object() ||
        !payload.contains("username") ||
        !payload["username"].is_string()) {
        send_error(client,
                   "BAD_REQUEST",
                   "missing or invalid username");
        return false;
    }

    const std::string username =
        trim(payload["username"].get<std::string>());

    if (!is_valid_username(username)) {
        send_login_response(client, false, "invalid username");
        return false;
    }

    if (!g_users.add(username, client)) {
        send_login_response(client,
                            false,
                            "username already exists");
        return false;
    }

    // 先标记登录成功。即使之后发送响应失败，LoginGuard 也会清理用户表。
    login_guard.mark_logged_in();

    send_login_response(client, true, "login success");
    log_line("user logged in: " + username);
    return true;
}

void handle_list_users(
    const std::shared_ptr<ClientConnection>& client) {
    const nlohmann::json payload = {
        {"users", g_users.usernames()},
    };

    send_json_frame(client,
                    chat::MessageType::kUserListResponse,
                    payload);
}

void remove_unreachable_client(
    const std::shared_ptr<ClientConnection>& client,
    const std::exception& error) {
    const std::optional<std::string> removed =
        g_users.remove_by_fd(client->fd());

    if (removed.has_value()) {
        log_line("removed unreachable user: " + removed.value() +
                 ", reason: " + error.what());
    }
}

void broadcast_chat(
    const std::string& from,
    const std::string& content) {
    const nlohmann::json payload = {
        {"from", from},
        {"to", "*"},
        {"content", content},
    };

    const chat::Frame frame{
        chat::MessageType::kChatMessage,
        payload.dump(),
    };

    const std::vector<std::shared_ptr<ClientConnection>> clients =
        g_users.clients();

    for (const auto& client : clients) {
        try {
            send_frame_to_client(client, frame);
        } catch (const std::exception& error) {
            remove_unreachable_client(client, error);
        }
    }
}

void send_private_chat(
    const std::shared_ptr<ClientConnection>& sender,
    const std::string& from,
    const std::string& to,
    const std::string& content) {
    const std::shared_ptr<ClientConnection> target =
        g_users.find_client(to);

    if (!target) {
        send_error(sender,
                   "USER_NOT_FOUND",
                   "target user is not online");
        return;
    }

    const nlohmann::json payload = {
        {"from", from},
        {"to", to},
        {"content", content},
    };

    const chat::Frame frame{
        chat::MessageType::kChatMessage,
        payload.dump(),
    };

    try {
        send_frame_to_client(target, frame);
    } catch (const std::exception& error) {
        remove_unreachable_client(target, error);

        send_error(sender,
                   "USER_NOT_FOUND",
                   "target user is not online");
    }
}

void handle_chat(
    const std::shared_ptr<ClientConnection>& sender,
    const chat::Frame& frame) {
    nlohmann::json payload;

    try {
        payload = nlohmann::json::parse(frame.payload);
    } catch (const nlohmann::json::exception&) {
        send_error(sender, "BAD_REQUEST", "invalid JSON payload");
        return;
    }

    if (!payload.is_object() ||
        !payload.contains("to") ||
        !payload["to"].is_string() ||
        !payload.contains("content") ||
        !payload["content"].is_string()) {
        send_error(sender,
                   "BAD_REQUEST",
                   "missing or invalid chat fields");
        return;
    }

    const std::string to = trim(payload["to"].get<std::string>());
    const std::string content = payload["content"].get<std::string>();

    if (to.empty()) {
        send_error(sender,
                   "BAD_REQUEST",
                   "chat target must not be empty");
        return;
    }

    if (content.size() > chat::kMaxChatContentSize) {
        send_error(sender,
                   "MESSAGE_TOO_LARGE",
                   "message content exceeds 1 MiB");
        return;
    }

    const std::optional<std::string> from =
        g_users.username_of(sender->fd());

    if (!from.has_value()) {
        send_error(sender,
                   "NOT_LOGGED_IN",
                   "please login first");
        return;
    }

    if (to == "*") {
        broadcast_chat(from.value(), content);
    } else {
        send_private_chat(sender, from.value(), to, content);
    }
}

void handle_client(std::shared_ptr<ClientConnection> client) {
    LoginGuard login_guard(g_users, client->fd());

    try {
        while (true) {
            chat::Frame frame;

            if (!chat::recv_frame(client->fd(), frame)) {
                return;
            }

            log_line("received frame: type=" +
                     std::string(chat::to_string(frame.type)) +
                     ", payload_size=" +
                     std::to_string(frame.payload.size()));

            if (!login_guard.is_logged_in()) {
                if (frame.type != chat::MessageType::kLoginRequest) {
                    send_error(client,
                               "NOT_LOGGED_IN",
                               "please login first");
                    continue;
                }

                if (!handle_login(client, frame, login_guard)) {
                    return;
                }

                continue;
            }

            switch (frame.type) {
                case chat::MessageType::kListUsersRequest:
                    handle_list_users(client);
                    break;

                case chat::MessageType::kSendChatRequest:
                    handle_chat(client, frame);
                    break;

                case chat::MessageType::kLoginRequest:
                    send_error(client,
                               "BAD_REQUEST",
                               "already logged in");
                    break;

                default:
                    send_error(client,
                               "UNKNOWN_TYPE",
                               "unknown message type");
                    break;
            }
        }
    } catch (const chat::ProtocolError& error) {
        send_error(client, "PROTOCOL_ERROR", error.what());
        log_line(std::string("protocol error: ") + error.what());
    } catch (const chat::NetworkError& error) {
        log_line(std::string("network error: ") + error.what());
    } catch (const std::exception& error) {
        log_line(std::string("unexpected client error: ") + error.what());
    }
}

std::string client_address_text(const sockaddr_in& address) {
    char ip[INET_ADDRSTRLEN] = {};

    if (::inet_ntop(AF_INET, &address.sin_addr, ip, sizeof(ip)) == nullptr) {
        return "unknown";
    }

    return std::string(ip) + ":" +
           std::to_string(ntohs(address.sin_port));
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc > 2) {
            std::cerr << "用法：" << argv[0] << " [端口]\n";
            return 1;
        }

        const std::uint16_t port =
            argc == 2 ? parse_port(argv[1]) : 9000;

        UniqueFd listener = create_listener(port);
        log_line("chat server listening on port " +
                 std::to_string(port));

        while (true) {
            sockaddr_in client_address{};
            socklen_t client_address_size = sizeof(client_address);

            const int raw_client_fd =
                ::accept(listener.get(),
                         reinterpret_cast<sockaddr*>(&client_address),
                         &client_address_size);

            if (raw_client_fd < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error(errno_message("accept"));
            }

            auto client =
                std::make_shared<ClientConnection>(raw_client_fd);

            log_line("accepted connection from " +
                     client_address_text(client_address));

            std::thread(handle_client, std::move(client)).detach();
        }
    } catch (const std::exception& error) {
        std::cerr << "server error: " << error.what() << '\n';
        return 1;
    }
}
