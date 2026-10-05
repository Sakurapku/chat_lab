#include "common/net.hpp"

#include <cerrno>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <utility>

namespace chat {

namespace {

std::string make_errno_message(const char* operation) {
    const int error_number = errno;
    return std::string(operation) + " failed: " +
           std::strerror(error_number);
}

}  // namespace

void send_all(int socket_fd, const std::string& data) {
    std::size_t total_sent = 0;

    while (total_sent < data.size()) {
        const char* next_position = data.data() + total_sent;
        const std::size_t remaining_size = data.size() - total_sent;

        const ssize_t sent =
            ::send(socket_fd, next_position, remaining_size, MSG_NOSIGNAL);

        if (sent > 0) {
            total_sent += static_cast<std::size_t>(sent);
            continue;
        }

        if (sent == 0) {
            throw NetworkError("send failed: sent zero bytes");
        }

        if (errno == EINTR) {
            continue;
        }

        throw NetworkError(make_errno_message("send"));
    }
}

bool recv_all(int socket_fd, std::size_t size, std::string& output) {
    output.assign(size, '\0');

    std::size_t total_received = 0;

    while (total_received < size) {
        char* next_position = output.data() + total_received;
        const std::size_t remaining_size = size - total_received;

        const ssize_t received =
            ::recv(socket_fd, next_position, remaining_size, 0);

        if (received > 0) {
            total_received += static_cast<std::size_t>(received);
            continue;
        }

        if (received == 0) {
            output.resize(total_received);
            return false;
        }

        if (errno == EINTR) {
            continue;
        }

        throw NetworkError(make_errno_message("recv"));
    }

    return true;
}

void send_frame(int socket_fd, const Frame& frame) {
    const std::string bytes = encode_frame(frame);
    send_all(socket_fd, bytes);
}

bool recv_frame(int socket_fd, Frame& frame) {
    std::string header_bytes;

    if (!recv_all(socket_fd, kHeaderSize, header_bytes)) {
        return false;
    }

    const Header header = decode_header(header_bytes);

    std::string payload;
    if (header.payload_size > 0) {
        if (!recv_all(socket_fd, header.payload_size, payload)) {
            throw NetworkError(
                "connection closed while receiving frame payload");
        }
    }

    frame.type = header.type;
    frame.payload = std::move(payload);
    return true;
}

}  // namespace chat
