#include "common/net.hpp"
#include "common/protocol.hpp"
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
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

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

void send_json_frame(int socket_fd,
                     chat::MessageType type,
                     const nlohmann::json& payload) {
    const chat::Frame frame{type, payload.dump()};
    chat::send_frame(socket_fd, frame);
}

void send_error(int socket_fd,
                const std::string& code,
                const std::string& message) {
    try {
        const nlohmann::json payload = {
            {"code", code},
            {"message", message},
        };

        send_json_frame(socket_fd,
                        chat::MessageType::kErrorMessage,
                        payload);
    } catch (const std::exception&) {
        // 当前连接可能已经损坏，发送错误失败时不再继续抛出异常。
    }
}

void send_login_response(int socket_fd,
                         bool ok,
                         const std::string& message) {
    const nlohmann::json payload = {
        {"ok", ok},
        {"message", message},
    };

    send_json_frame(socket_fd,
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

bool handle_login(int socket_fd,
                  const chat::Frame& frame,
                  LoginGuard& login_guard) {
    nlohmann::json payload;

    try {
        payload = nlohmann::json::parse(frame.payload);
    } catch (const nlohmann::json::exception&) {
        send_error(socket_fd, "BAD_REQUEST", "invalid JSON payload");
        return false;
    }

    if (!payload.is_object() ||
        !payload.contains("username") ||
        !payload["username"].is_string()) {
        send_error(socket_fd,
                   "BAD_REQUEST",
                   "missing or invalid username");
        return false;
    }

    const std::string username =
        trim(payload["username"].get<std::string>());

    if (!is_valid_username(username)) {
        send_login_response(socket_fd, false, "invalid username");
        return false;
    }

    if (!g_users.add(username, socket_fd)) {
        send_login_response(socket_fd,
                            false,
                            "username already exists");
        return false;
    }

    // 先标记登录成功。即使之后发送响应失败，LoginGuard 也会清理用户表。
    login_guard.mark_logged_in();

    send_login_response(socket_fd, true, "login success");
    log_line("user logged in: " + username);
    return true;
}

void handle_list_users(int socket_fd) {
    const nlohmann::json payload = {
        {"users", g_users.usernames()},
    };

    send_json_frame(socket_fd,
                    chat::MessageType::kUserListResponse,
                    payload);
}

void handle_client(UniqueFd client) {
    LoginGuard login_guard(g_users, client.get());

    try {
        while (true) {
            chat::Frame frame;

            if (!chat::recv_frame(client.get(), frame)) {
                return;
            }

            log_line("received frame: type=" +
                     std::string(chat::to_string(frame.type)) +
                     ", payload_size=" +
                     std::to_string(frame.payload.size()));

            if (!login_guard.is_logged_in()) {
                if (frame.type != chat::MessageType::kLoginRequest) {
                    send_error(client.get(),
                               "NOT_LOGGED_IN",
                               "please login first");
                    continue;
                }

                if (!handle_login(client.get(), frame, login_guard)) {
                    return;
                }

                continue;
            }

            switch (frame.type) {
                case chat::MessageType::kListUsersRequest:
                    handle_list_users(client.get());
                    break;

                case chat::MessageType::kSendChatRequest:
                    send_error(client.get(),
                               "BAD_REQUEST",
                               "chat will be implemented in the next step");
                    break;

                case chat::MessageType::kLoginRequest:
                    send_error(client.get(),
                               "BAD_REQUEST",
                               "already logged in");
                    break;

                default:
                    send_error(client.get(),
                               "UNKNOWN_TYPE",
                               "unknown message type");
                    break;
            }
        }
    } catch (const chat::ProtocolError& error) {
        send_error(client.get(), "PROTOCOL_ERROR", error.what());
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

            UniqueFd client(raw_client_fd);

            log_line("accepted connection from " +
                     client_address_text(client_address));

            std::thread(handle_client, std::move(client)).detach();
        }
    } catch (const std::exception& error) {
        std::cerr << "server error: " << error.what() << '\n';
        return 1;
    }
}
