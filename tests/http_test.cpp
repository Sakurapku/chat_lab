#include "web/http.hpp"

#include <cassert>
#include <iostream>

int main() {
    HttpRequest request;

    std::string raw =
        "GET /api/users HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "Content-Type: application/json\r\n"
        "\r\n";

    bool ok = parse_http_header(raw, request);

    assert(ok);
    assert(request.method == "GET");
    assert(request.path == "/api/users");
    assert(request.version == "HTTP/1.1");
    assert(request.headers.at("host") == "localhost:8080");
    assert(request.headers.at("content-type") == "application/json");

    std::cout << "HTTP header test passed!\n";

    return 0;
}
