#include "web/router.hpp"

#include <cassert>
#include <iostream>

int main() {
    HttpRequest request;
    HttpResponse response;

    // 测试 1：健康检查
    request.method = "GET";
    request.path = "/api/health";

    response = route_request(request);

    assert(response.status == 200);
    assert(response.body.find("\"ok\"") != std::string::npos);

    // 测试 2：正常 JSON 消息
    request = HttpRequest{};
    request.method = "POST";
    request.path = "/api/echo";
    request.headers["content-type"] = "application/json";
    request.body = R"({"message":"hello"})";

    response = route_request(request);

    assert(response.status == 200);
    assert(response.body.find("hello") != std::string::npos);

    // 测试 3：非法 JSON
    request.body = "{invalid json}";

    response = route_request(request);

    assert(response.status == 400);

    // 测试 4：缺少 message
    request.body = R"({"username":"bob"})";

    response = route_request(request);

    assert(response.status == 400);

    // 测试 5：message 类型错误
    request.body = R"({"message":123})";

    response = route_request(request);

    assert(response.status == 400);

    // 测试 6：消息过长
    request.body = "";
    request.body = R"({"message":")" +
                   std::string(1024 * 1024 + 1, 'a') +
                   R"("})";

    response = route_request(request);

    assert(response.status == 413);

    // 测试 7：不支持的请求方法
    request = HttpRequest{};
    request.method = "GET";
    request.path = "/api/echo";

    response = route_request(request);

    assert(response.status == 405);

    // 测试 8：不存在的路径
    request.path = "/api/unknown";

    response = route_request(request);

    assert(response.status == 404);

    std::cout << "All router tests passed!\n";

    return 0;
}
