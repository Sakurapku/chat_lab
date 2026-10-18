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
