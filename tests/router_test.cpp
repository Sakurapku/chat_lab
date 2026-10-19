
#include "web/router.hpp"

#include <cassert>
#include <iostream>
#include <string>

int main() {
    // 创建账户管理对象，供所有测试使用
    AccountStore accounts;

    HttpRequest request;
    HttpResponse response;

    // 测试 1：健康检查
    request.method = "GET";
    request.path = "/api/health";

    response = route_request(request, accounts);

    assert(response.status == 200);
    assert(response.body.find("\"ok\"") != std::string::npos);

    // 测试 2：正常 JSON 消息
    request = HttpRequest{};
    request.method = "POST";
    request.path = "/api/echo";
    request.headers["content-type"] = "application/json";
    request.body = R"({"message":"hello"})";

    response = route_request(request, accounts);

    assert(response.status == 200);
    assert(response.body.find("hello") != std::string::npos);

    // 测试 3：非法 JSON
    request.body = "{invalid json}";

    response = route_request(request, accounts);

    assert(response.status == 400);

    // 测试 4：缺少 message 字段
    request.body = R"({"username":"bob"})";

    response = route_request(request, accounts);

    assert(response.status == 400);

    // 测试 5：message 类型错误
    request.body = R"({"message":123})";

    response = route_request(request, accounts);

    assert(response.status == 400);

    // 测试 6：消息过长
    request.body = R"({"message":")" +
                   std::string(1024 * 1024 + 1, 'a') +
                   R"("})";

    response = route_request(request, accounts);

    assert(response.status == 413);

    // 测试 7：不支持的请求方法
    request = HttpRequest{};
    request.method = "GET";
    request.path = "/api/echo";

    response = route_request(request, accounts);

    assert(response.status == 405);

    // 测试 8：不存在的路径
    request.path = "/api/unknown";

    response = route_request(request, accounts);

    assert(response.status == 404);

    // 测试 9：正常注册用户
    request = HttpRequest{};
    request.method = "POST";
    request.path = "/api/register";
    request.headers["content-type"] = "application/json";
    request.body =
        R"({"username":"alice","password":"password123"})";

    response = route_request(request, accounts);

    assert(response.status == 201);
    assert(response.body.find("Registration successful")
           != std::string::npos);

    // 测试 10：重复注册用户名
    response = route_request(request, accounts);

    assert(response.status == 409);

    // 测试 11：用户名不合法
    request.body =
        R"({"username":"a!","password":"password123"})";

    response = route_request(request, accounts);

    assert(response.status == 400);

    // 测试 12：密码太短
    request.body =
        R"({"username":"bob","password":"123"})";

    response = route_request(request, accounts);

    assert(response.status == 400);

    // 测试 13：缺少密码字段
    request.body = R"({"username":"bob"})";

    response = route_request(request, accounts);

    assert(response.status == 400);

    // 测试 14：注册时密码类型错误
    request.body =
        R"({"username":"bob","password":12345678})";

    response = route_request(request, accounts);

    assert(response.status == 400);

    // 测试 15：注册接口不支持 GET
    request = HttpRequest{};
    request.method = "GET";
    request.path = "/api/register";

    response = route_request(request, accounts);

    assert(response.status == 405);

    std::cout << "All router tests passed!\n";

    return 0;
}
