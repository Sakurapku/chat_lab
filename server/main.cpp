#include "common/net.hpp"
#include "common/protocol.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

std::mutex g_log_mutex;

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

std::uint16_t parse_port(const char* text) {
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);

    if (text == nullptr || *text == '\0' || end == text ||
        *end != '\0' || value < 1 || value > 65535) {
        throw std::invalid_argument("port must be an integer between 1 and 65535");
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

void send_error(int socket_fd,
                const std::string& code,
                const std::string& message) {
    try {
        const nlohmann::json payload = {
            {"code", code},
            {"message", message},
        };

        const chat::Frame frame{
            chat::MessageType::kErrorMessage,
            payload.dump(),
        };

        chat::send_frame(socket_fd, frame);
    } catch (const std::exception&) {
        // 当前连接本来就可能已经损坏，发送错误失败时不再继续抛出异常。
    }
}

void handle_client(UniqueFd client) {
    try {
        while (true) {
            chat::Frame frame;

            if (!chat::recv_frame(client.get(), frame)) {
                log_line("client disconnected");
                return;
            }

            log_line("received frame: type=" +
                     std::string(chat::to_string(frame.type)) +
                     ", payload_size=" +
                     std::to_string(frame.payload.size()));

            switch (frame.type) {
                case chat::MessageType::kLoginRequest:
                case chat::MessageType::kListUsersRequest:
                case chat::MessageType::kSendChatRequest:
                    send_error(client.get(),
                               "BAD_REQUEST",
                               "server business logic is not implemented yet");
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
