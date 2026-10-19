#include "web/http.hpp"


#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <limits>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>

// 将 HTTP 请求头名称转换成小写
std::string to_lower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return str;
}

// 去掉字符串首尾的空格和 Tab
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t");

    if (first == std::string::npos) {
        return "";
    }

    size_t last = str.find_last_not_of(" \t");

    return str.substr(first, last - first + 1);
}

bool parse_http_header(const std::string& raw,
                       HttpRequest& request) {
    if (raw.size() > MAX_HEADER_SIZE) {
        return false;
    }

    // HTTP 请求头必须以 \r\n\r\n 结束
    if (raw.size() < 4 ||
        raw.compare(raw.size() - 4, 4, "\r\n\r\n") != 0) {
        return false;
    }

    std::istringstream stream(raw);
    std::string line;

    // 读取第一行：请求行
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

    // 请求行必须恰好有三个字段
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

    // 逐行读取请求头
    while (std::getline(stream, line)) {
        if (line.empty() || line.back() != '\r') {
            return false;
        }

        line.pop_back();

        // 遇到空行，说明请求头结束
        if (line.empty()) {
            request = std::move(result);
            return true;
        }

        size_t pos = line.find(':');

        if (pos == std::string::npos || pos == 0) {
            return false;
        }

        std::string key = to_lower(line.substr(0, pos));
        std::string value = trim(line.substr(pos + 1));

        // 暂时拒绝重复的请求头字段
        if (result.headers.count(key) != 0) {
            return false;
        }

        result.headers[key] = value;
    }

    return false;
}

// 安全地解析十进制 Content-Length
bool parse_content_length(const std::string& str,
                          std::size_t& length) {
    if (str.empty()) {
        return false;
    }

    std::size_t result = 0;

    for (char c : str) {
        if (c < '0' || c > '9') {
            return false;
        }

        std::size_t digit = static_cast<std::size_t>(c - '0');

        // 防止整数溢出
        if (result > (std::numeric_limits<std::size_t>::max()
                      - digit) / 10) {
            return false;
        }

        result = result * 10 + digit;
    }

    length = result;
    return true;
}

HttpReadResult read_http_request(int client_fd,
                                 HttpRequest& request) {
    std::string data;
    char buffer[4096];

    std::size_t header_end = std::string::npos;

    // 第一部分：循环读取完整请求头
    while (true) {
        header_end = data.find("\r\n\r\n");

        if (header_end != std::string::npos) {
            break;
        }

        if (data.size() >= MAX_HEADER_SIZE) {
            return HttpReadResult::TooLarge;
        }

        // 限制本次接收量，避免超过请求头上限
        std::size_t remaining = MAX_HEADER_SIZE - data.size();

        std::size_t capacity =
            remaining < sizeof(buffer) ? remaining : sizeof(buffer);

        ssize_t n = recv(client_fd, buffer, capacity, 0);

        if (n > 0) {
            data.append(buffer, static_cast<std::size_t>(n));
        } else if (n == 0) {
            return HttpReadResult::BadRequest;
        } else if (errno == EINTR) {
            continue;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return HttpReadResult::Timeout;
        } else {
            return HttpReadResult::IoError;
        }
    }

    // HTTP 请求头包括末尾的 \r\n\r\n
    std::size_t header_length = header_end + 4;

    if (header_length > MAX_HEADER_SIZE) {
        return HttpReadResult::TooLarge;
    }

    // 第二部分：解析请求头
    std::string raw_header = data.substr(0, header_length);

    if (!parse_http_header(raw_header, request)) {
        return HttpReadResult::BadRequest;
    }

    // 本阶段只支持 Content-Length 定界的请求体
    if (request.headers.count("transfer-encoding") != 0) {
        return HttpReadResult::BadRequest;
    }

    // HTTP/1.1 要求 Host 请求头
    if (request.headers.count("host") == 0 ||
        request.headers.at("host").empty()) {
        return HttpReadResult::BadRequest;
    }

    // 第三部分：获取请求体长度
    std::size_t content_length = 0;

    auto it = request.headers.find("content-length");

    if (it != request.headers.end()) {
        if (!parse_content_length(it->second, content_length)) {
            return HttpReadResult::BadRequest;
        }
    } else if (request.method == "POST") {
        // 本项目要求 POST 请求明确声明正文长度
        return HttpReadResult::BadRequest;
    }

    if (content_length > MAX_BODY_SIZE) {
        return HttpReadResult::TooLarge;
    }

    // 第四部分：处理接收请求头时已经读到的正文
    std::string body = data.substr(header_length);

    if (body.size() > content_length) {
        return HttpReadResult::BadRequest;
    }

    // 第五部分：继续读取剩余正文
    while (body.size() < content_length) {
        std::size_t remaining = content_length - body.size();

        std::size_t capacity =
            remaining < sizeof(buffer) ? remaining : sizeof(buffer);

        ssize_t n = recv(client_fd, buffer, capacity, 0);

        if (n > 0) {
            body.append(buffer, static_cast<std::size_t>(n));
        } else if (n == 0) {
            return HttpReadResult::BadRequest;
        } else if (errno == EINTR) {
            continue;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return HttpReadResult::Timeout;
        } else {
            return HttpReadResult::IoError;
        }
    }

    request.body = std::move(body);

    return HttpReadResult::Ok;
}

std::string build_http_response(const HttpResponse& response) {
    std::string reason;

    switch (response.status) {
        case 200:
            reason = "OK";
            break;
        case 400:
            reason = "Bad Request";
            break;
        case 404:
            reason = "Not Found";
            break;
        case 408:
            reason = "Request Timeout";
            break;
        case 413:
            reason = "Content Too Large";
            break;
        case 431:
            reason = "Request Header Fields Too Large";
            break;
        case 405:
            reason = "Method Not Allowed";
            break;
        case 201:
            reason = "Created";
    	    break;
 	case 409:
            reason = "Conflict";
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
        + "X-Content-Type-Options: nosniff\r\n"
        + "\r\n"
        + response.body;

    return result;
}
