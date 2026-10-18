#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <iostream>
#include <string>

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0) {
        std::cerr << "socket 创建失败\n";
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(8080);
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_fd,
             reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) < 0) {
        std::cerr << "bind 失败\n";
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        std::cerr << "listen 失败\n";
        close(server_fd);
        return 1;
    }

    std::cout << "HTTP server listening on port 8080...\n";

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);

        if (client_fd < 0) {
            std::cerr << "accept 失败\n";
            continue;
        }

        // 1. 接收客户端发送的数据
        char buffer[4096];

        ssize_t n = recv(client_fd, buffer, sizeof(buffer), 0);

        if (n <= 0) {
            close(client_fd);
            continue;
        }

        std::string request(buffer, static_cast<size_t>(n));

        std::cout << "收到 HTTP 请求：\n";
        std::cout << request << '\n';

        // 2. 构造 HTTP 响应正文
        std::string body = "Hello from chat_lab!";

        // 3. 构造 HTTP 响应
        std::string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n"
            "Connection: close\r\n"
            "\r\n" + body;

        // 4. 发送完整的 HTTP 响应
        size_t sent = 0;

        while (sent < response.size()) {
            ssize_t nsend = send(
                client_fd,
                response.data() + sent,
                response.size() - sent,
                MSG_NOSIGNAL
            );

            if (nsend > 0) {
                sent += static_cast<size_t>(nsend);
            } else if (nsend < 0 && errno == EINTR) {
                continue;
            } else {
                std::cerr << "send 失败或连接已关闭\n";
                break;
            }
        }

        // 5. 关闭连接
        close(client_fd);
    }

    close(server_fd);
    return 0;
}
