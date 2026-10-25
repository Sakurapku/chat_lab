#include "web/router.hpp"

#include <nlohmann/json.hpp>

#include <cassert>
#include <iostream>
#include <string>

using json = nlohmann::json;

int main() {
    AccountStore accounts;
    ChatStore chats;

    HttpRequest request;
    HttpResponse response;

    // 1. 健康检查
    request.method = "GET";
    request.path = "/api/health";

    response = route_request(request, accounts, chats);

    assert(response.status == 200);

    // 2. JSON 回显
    request = HttpRequest{};
    request.method = "POST";
    request.path = "/api/echo";
    request.headers["content-type"] = "application/json";
    request.body = R"({"message":"hello"})";

    response = route_request(request, accounts, chats);

    assert(response.status == 200);

    // 3. 非法 JSON
    request.body = "{invalid json}";

    response = route_request(request, accounts, chats);

    assert(response.status == 400);

    // 4. 消息类型错误
    request.body = R"({"message":123})";

    response = route_request(request, accounts, chats);

    assert(response.status == 400);

    // 5. 未知接口
    request = HttpRequest{};
    request.method = "GET";
    request.path = "/api/unknown";

    response = route_request(request, accounts, chats);

    assert(response.status == 404);

    // 6. 注册 Alice
    request = HttpRequest{};
    request.method = "POST";
    request.path = "/api/register";
    request.headers["content-type"] = "application/json";
    request.body =
        R"({"username":"alice","password":"password123"})";

    response = route_request(request, accounts, chats);

    assert(response.status == 201);

    // 7. 重复注册
    response = route_request(request, accounts, chats);

    assert(response.status == 409);

    // 8. 注册 Bob
    request.body =
        R"({"username":"bob","password":"password456"})";

    response = route_request(request, accounts, chats);

    assert(response.status == 201);

    // 9. 登录 Alice 和 Bob
    std::string alice_token;
    std::string bob_token;

    assert(
        accounts.login_user(
            "alice", "password123", alice_token
        ) == LoginResult::Success
    );

    assert(
        accounts.login_user(
            "bob", "password456", bob_token
        ) == LoginResult::Success
    );

    // 10. Alice 向 Bob 发送私聊
    request = HttpRequest{};
    request.method = "POST";
    request.path = "/api/messages";
    request.headers["content-type"] = "application/json";
    request.headers["authorization"] =
        "Bearer " + alice_token;

    // 故意伪造 from 为 Bob
    request.body = R"({
        "from":"bob",
        "to":"bob",
        "content":"hello bob"
    })";

    response = route_request(request, accounts, chats);

    assert(response.status == 200);

    json sent = json::parse(response.body);

    // 服务端必须识别真实发送者 Alice
    assert(sent["from"] == "alice");
    assert(sent["type"] == "private");

    // 11. Bob 读取自己的消息
    request = HttpRequest{};
    request.method = "GET";
    request.path = "/api/messages";
    request.headers["authorization"] =
        "Bearer " + bob_token;

    response = route_request(request, accounts, chats);

    assert(response.status == 200);

    json received = json::parse(response.body);

    assert(received["count"] == 1);
    assert(received["messages"][0]["from"] == "alice");
    assert(received["messages"][0]["to"] == "bob");
    assert(received["messages"][0]["content"] == "hello bob");

    // 12. 再次读取，消息已从队列取走
    response = route_request(request, accounts, chats);

    received = json::parse(response.body);

    assert(received["count"] == 0);

    // 13. 没有 Token 不能读取消息
    request.headers.clear();

    response = route_request(request, accounts, chats);

    assert(response.status == 401);

    // 14. Alice 发送群聊
    request = HttpRequest{};
    request.method = "POST";
    request.path = "/api/messages";
    request.headers["content-type"] = "application/json";
    request.headers["authorization"] =
        "Bearer " + alice_token;

    request.body = R"({"content":"hello everyone"})";

    response = route_request(request, accounts, chats);

    assert(response.status == 200);

    sent = json::parse(response.body);

    assert(sent["type"] == "group");

    // 15. Bob 收到群聊
    request = HttpRequest{};
    request.method = "GET";
    request.path = "/api/messages";
    request.headers["authorization"] =
        "Bearer " + bob_token;

    response = route_request(request, accounts, chats);

    received = json::parse(response.body);

    assert(received["count"] == 1);
    assert(received["messages"][0]["type"] == "group");

    // 16. 不在线的用户无法接收私聊
    request = HttpRequest{};
    request.method = "POST";
    request.path = "/api/messages";
    request.headers["content-type"] = "application/json";
    request.headers["authorization"] =
        "Bearer " + alice_token;

    request.body = R"({
        "to":"nobody",
        "content":"hello"
    })";

    response = route_request(request, accounts, chats);

    assert(response.status == 404);

    // 17. 空消息被拒绝
    request.body = R"({"content":""})";

    response = route_request(request, accounts, chats);

    assert(response.status == 400);

    // 18. 检查消息长度限制
    std::string large_message(
        ChatStore::MAX_MESSAGE_SIZE + 1,
        'a'
    );

    json data;
    data["content"] = large_message;

    request.body = data.dump();

    response = route_request(request, accounts, chats);

    assert(response.status == 413);

    // 19. 已注销的 Token 无法发送
    assert(accounts.logout_user(alice_token));

    request.body = R"({"content":"hello"})";

    response = route_request(request, accounts, chats);

    assert(response.status == 401);

    std::cout << "All router tests passed!\n";

    return 0;
}
