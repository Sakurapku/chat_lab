#include "web/http.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstddef>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

// 请求头名称统一转为小写
std::string to_lower(std::string str) {
    std::transform(
        str.begin(), str.end(), str.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        }
    );

    return str;
}

// 去除首尾空格和 Tab
std::string trim(const std::string& str) {
    std::size_t first = str.find_first_not_of(" \t");

    if (first == std::string::npos) {
        return "";
    }

    std::size_t last = str.find_last_not_of(" \t");

    return str.substr(first, last - first + 1);
}

// 解析 HTTP 请求头
bool parse_http_header(
    const std::string& raw,
    HttpRequest& request
) {
    if (raw.size() > MAX_HEADER_SIZE) {
        return false;
    }

    if (raw.size() < 4 ||
        raw.compare(raw.size() - 4, 4, "\r\n\r\n") != 0) {
        return false;
    }

    std::istringstream stream(raw);
    std::string line;

    if (!std::getline(stream, line)) {
        return false;
    }

    if (line.empty() || line.back() != '\r') {
        return false;
    }

    line.pop_back();

    HttpRequest result;

    std::istringstream request_line(line);
    std::string extra;

    if (!(request_line >> result.method
                       >> result.path
                       >> result.version)) {
        return false;
    }

    if (request_line >> extra) {
        return false;
    }

    if (result.method != "GET" &&
        result.method != "POST") {
        return false;
    }

    if (result.version != "HTTP/1.1") {
        return false;
    }

    if (result.path.empty() || result.path[0] != '/') {
        return false;
    }

    bool found_end = false;

    while (std::getline(stream, line)) {
        if (line.empty() || line.back() != '\r') {
            return false;
        }

        line.pop_back();

        if (line.empty()) {
            found_end = true;
            break;
        }

        std::size_t pos = line.find(':');

        if (pos == std::string::npos || pos == 0) {
            return false;
        }

        std::string key = line.substr(0, pos);

        // 请求头名称中不允许空格及控制字符
        for (unsigned char c : key) {
            if (c <= 32 || c >= 127) {
                return false;
            }
        }

        key = to_lower(key);

        std::string value = trim(line.substr(pos + 1));

        // 简化实现：拒绝重复请求头，避免歧义
        if (result.headers.count(key) != 0) {
            return false;
        }

        result.headers[key] = value;
    }

    if (!found_end) {
        return false;
    }

    request = std::move(result);

    return true;
}

// 安全解析 Content-Length
bool parse_content_length(
    const std::string& str,
    std::size_t& length
) {
    if (str.empty()) {
        return false;
    }

    std::size_t result = 0;

    for (char c : str) {
        if (c < '0' || c > '9') {
            return false;
        }

        std::size_t digit =
            static_cast<std::size_t>(c - '0');

        if (result >
            (std::numeric_limits<std::size_t>::max() - digit) / 10) {
            return false;
        }

        result = result * 10 + digit;
    }

    length = result;

    return true;
}

// 从 TCP 连接读取完整 HTTP 请求
HttpReadResult read_http_request(
    int client_fd,
    HttpRequest& request
) {
    std::string data;
    char buffer[4096];

    std::size_t header_end = std::string::npos;

    while (true) {
        header_end = data.find("\r\n\r\n");

        if (header_end != std::string::npos) {
            break;
        }

        if (data.size() >= MAX_HEADER_SIZE) {
            return HttpReadResult::TooLarge;
        }

        std::size_t remaining =
            MAX_HEADER_SIZE - data.size();

        std::size_t capacity =
            std::min(remaining, sizeof(buffer));

        ssize_t n = recv(client_fd, buffer, capacity, 0);

        if (n > 0) {
            data.append(buffer, static_cast<std::size_t>(n));
        } else if (n == 0) {
            return HttpReadResult::BadRequest;
        } else if (errno == EINTR) {
            continue;
        } else if (errno == EAGAIN ||
                   errno == EWOULDBLOCK) {
            return HttpReadResult::Timeout;
        } else {
            return HttpReadResult::IoError;
        }
    }

    std::size_t header_length = header_end + 4;

    if (header_length > MAX_HEADER_SIZE) {
        return HttpReadResult::TooLarge;
    }

    std::string raw_header =
        data.substr(0, header_length);

    if (!parse_http_header(raw_header, request)) {
        return HttpReadResult::BadRequest;
    }

    // 当前版本不支持 chunked 请求体
    if (request.headers.count("transfer-encoding") != 0) {
        return HttpReadResult::BadRequest;
    }

    auto host = request.headers.find("host");

    if (host == request.headers.end() ||
        host->second.empty()) {
        return HttpReadResult::BadRequest;
    }

    std::size_t content_length = 0;

    auto it = request.headers.find("content-length");

    if (it != request.headers.end()) {
        if (!parse_content_length(
                it->second, content_length
            )) {
            return HttpReadResult::BadRequest;
        }
    } else if (request.method == "POST") {
        return HttpReadResult::BadRequest;
    }

    if (content_length > MAX_BODY_SIZE) {
        return HttpReadResult::TooLarge;
    }

    std::string body = data.substr(header_length);

    if (body.size() > content_length) {
        return HttpReadResult::BadRequest;
    }

    while (body.size() < content_length) {
        std::size_t remaining =
            content_length - body.size();

        std::size_t capacity =
            std::min(remaining, sizeof(buffer));

        ssize_t n = recv(client_fd, buffer, capacity, 0);

        if (n > 0) {
            body.append(buffer, static_cast<std::size_t>(n));
        } else if (n == 0) {
            return HttpReadResult::BadRequest;
        } else if (errno == EINTR) {
            continue;
        } else if (errno == EAGAIN ||
                   errno == EWOULDBLOCK) {
            return HttpReadResult::Timeout;
        } else {
            return HttpReadResult::IoError;
        }
    }

    request.body = std::move(body);

    return HttpReadResult::Ok;
}

// 构造 HTTP 响应字符串
std::string build_http_response(
    const HttpResponse& response
) {
    std::string reason;

    switch (response.status) {
        case 200:
            reason = "OK";
            break;

        case 201:
            reason = "Created";
            break;

        case 400:
            reason = "Bad Request";
            break;

        case 401:
            reason = "Unauthorized";
            break;

        case 404:
            reason = "Not Found";
            break;

        case 405:
            reason = "Method Not Allowed";
            break;

        case 408:
            reason = "Request Timeout";
            break;

        case 409:
            reason = "Conflict";
            break;

        case 413:
            reason = "Content Too Large";
            break;

        case 431:
            reason = "Request Header Fields Too Large";
            break;

        case 500:
            reason = "Internal Server Error";
            break;

        default:
            reason = "Internal Server Error";
            break;
    }

    std::string result =
        "HTTP/1.1 " + std::to_string(response.status)
        + " " + reason + "\r\n"
        + "Content-Type: " + response.content_type + "\r\n"
        + "Content-Length: "
        + std::to_string(response.body.size()) + "\r\n"
        + "Connection: close\r\n"
        + "Cache-Control: no-store\r\n"
        + "X-Content-Type-Options: nosniff\r\n"
        + "\r\n"
        + response.body;

    return result;
}
