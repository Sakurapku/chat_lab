#include "web/http.hpp"

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
