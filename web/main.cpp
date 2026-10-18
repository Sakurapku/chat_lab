#include "web/http.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <iostream>
#include <string>

// 发送完整响应
bool send_all_http(int fd, const std::string& data) {
    std::size_t sent = 0;

    while (sent < data.size()) {
        ssize_t n = send(fd,
                         data.data() + sent,
                         data.size() - sent,
                         MSG_NOSIGNAL);

        if (n > 0) {
            sent += static_cast<std::size_t>(n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }

    return true;
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0) {
        std::cerr << "socket failed\n";
        return 1;
    }

    // 允许程序重启后尽快重新绑定端口
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) < 0) {
        std::cerr << "setsockopt failed\n";
        close(server_fd);
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(8080);
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_fd,
             reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) < 0) {
        std::cerr << "bind failed\n";
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        std::cerr << "listen failed\n";
        close(server_fd);
        return 1;
    }

    std::cout << "HTTP server listening on port 8080...\n";

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);

        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "accept failed\n";
            continue;
        }

        // 为接收和发送设置 5 秒操作超时
        timeval timeout{};
        timeout.tv_sec = 5;

        if (setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO,
                       &timeout, sizeof(timeout)) < 0 ||
            setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO,
                       &timeout, sizeof(timeout)) < 0) {
            close(client_fd);
            continue;
        }

        HttpRequest request;
        HttpResponse response;

        HttpReadResult result = read_http_request(client_fd, request);

        if (result == HttpReadResult::Ok) {
            std::cout << "method: " << request.method << '\n';
            std::cout << "path: " << request.path << '\n';
            std::cout << "body size: " << request.body.size() << '\n';

            if (request.method == "GET" && request.path == "/") {
                response.status = 200;
                response.body = "Hello from chat_lab!";
            } else {
                response.status = 404;
                response.body = "Not Found";
            }

        } else if (result == HttpReadResult::TooLarge) {
            response.status = 413;
            response.body = "Request Too Large";

        } else if (result == HttpReadResult::Timeout) {
            response.status = 408;
            response.body = "Request Timeout";

        } else if (result == HttpReadResult::BadRequest) {
            response.status = 400;
            response.body = "Bad Request";

        } else {
            // 网络异常，不再尝试发送响应
            close(client_fd);
            continue;
        }

        std::string data = build_http_response(response);

        if (!send_all_http(client_fd, data)) {
            std::cerr << "Failed to send HTTP response\n";
        }

        close(client_fd);
    }

    close(server_fd);
    return 0;
}
