#pragma once

#include <cstddef>
#include <map>
#include <string>

struct HttpRequest {
    std::string method;
    std::string path;
    std::string version;

    std::map<std::string, std::string> headers;

    std::string body;
};

struct HttpResponse {
    int status = 200;
    std::string content_type = "text/plain; charset=utf-8";
    std::string body;
};

constexpr std::size_t MAX_HEADER_SIZE = 16 * 1024;
constexpr std::size_t MAX_BODY_SIZE = 2 * 1024 * 1024;

bool parse_http_header(const std::string& raw,
                       HttpRequest& request);

enum class HttpReadResult {
    Ok,
    BadRequest,
    TooLarge,
    Timeout,
    IoError
};

// 从 TCP 连接中读取一条完整的 HTTP 请求
HttpReadResult read_http_request(int client_fd,
                                 HttpRequest& request);

// 将 HTTP 响应转换为可以发送的字符串
std::string build_http_response(const HttpResponse& response);
