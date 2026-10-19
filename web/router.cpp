
#include "web/router.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>

using json = nlohmann::json;

// 创建 JSON 格式的 HTTP 响应
HttpResponse make_json_response(int status, const json& data) {
    HttpResponse response;

    response.status = status;
    response.content_type = "application/json; charset=utf-8";
    response.body = data.dump();

    return response;
}

// 创建错误响应
HttpResponse make_error(int status, const std::string& message) {
    return make_json_response(status, {
        {"error", message}
    });
}

// 处理 HTTP 请求
HttpResponse route_request(
    const HttpRequest& request,
    AccountStore& accounts
) {
    // 1. 首页
    if (request.path == "/") {
        if (request.method != "GET") {
            return make_error(405, "Method Not Allowed");
        }

        HttpResponse response;
        response.status = 200;
        response.body = "Hello from chat_lab!";

        return response;
    }

    // 2. 健康检查接口
    if (request.path == "/api/health") {
        if (request.method != "GET") {
            return make_error(405, "Method Not Allowed");
        }

        return make_json_response(200, {
            {"status", "ok"},
            {"service", "chat_lab"}
        });
    }

    // 3. JSON 回显接口
    if (request.path == "/api/echo") {
        if (request.method != "POST") {
            return make_error(405, "Method Not Allowed");
        }

        auto it = request.headers.find("content-type");

        if (it == request.headers.end()) {
            return make_error(400, "Missing Content-Type");
        }

        std::string content_type = it->second;
        std::size_t semicolon = content_type.find(';');

        content_type = content_type.substr(0, semicolon);

        std::size_t first =
            content_type.find_first_not_of(" \t");
        std::size_t last =
            content_type.find_last_not_of(" \t");

        if (first == std::string::npos) {
            return make_error(400, "Invalid Content-Type");
        }

        content_type = content_type.substr(
            first, last - first + 1
        );

        if (content_type != "application/json") {
            return make_error(
                400, "Content-Type must be application/json"
            );
        }

        json data = json::parse(
            request.body, nullptr, false
        );

        if (data.is_discarded() || !data.is_object()) {
            return make_error(400, "Invalid JSON object");
        }

        if (!data.contains("message") ||
            !data["message"].is_string()) {
            return make_error(
                400, "Missing or invalid message"
            );
        }

        std::string message =
            data["message"].get<std::string>();

        constexpr std::size_t MAX_MESSAGE_SIZE =
            1024 * 1024;

        if (message.size() > MAX_MESSAGE_SIZE) {
            return make_error(413, "Message Too Large");
        }

        return make_json_response(200, {
            {"message", message}
        });
    }

    // 4. 用户注册接口
    if (request.path == "/api/register") {
        if (request.method != "POST") {
            return make_error(405, "Method Not Allowed");
        }

        // 检查 Content-Type
        auto it = request.headers.find("content-type");

        if (it == request.headers.end()) {
            return make_error(400, "Missing Content-Type");
        }

        std::string content_type = it->second;
        std::size_t semicolon = content_type.find(';');

        content_type = content_type.substr(0, semicolon);

        std::size_t first =
            content_type.find_first_not_of(" \t");
        std::size_t last =
            content_type.find_last_not_of(" \t");

        if (first == std::string::npos) {
            return make_error(400, "Invalid Content-Type");
        }

        content_type = content_type.substr(
            first, last - first + 1
        );

        if (content_type != "application/json") {
            return make_error(
                400, "Content-Type must be application/json"
            );
        }

        // 解析 JSON 请求体
        json data = json::parse(
            request.body, nullptr, false
        );

        if (data.is_discarded() || !data.is_object()) {
            return make_error(400, "Invalid JSON");
        }

        // 检查 username 和 password 字段
        if (!data.contains("username") ||
            !data["username"].is_string() ||
            !data.contains("password") ||
            !data["password"].is_string()) {
            return make_error(
                400, "Invalid username or password"
            );
        }

        std::string username =
            data["username"].get<std::string>();

        std::string password =
            data["password"].get<std::string>();

        // 调用账户管理模块注册用户
        RegisterResult result =
            accounts.register_user(username, password);

        // 根据注册结果返回 HTTP 响应
        switch (result) {
            case RegisterResult::Success:
                return make_json_response(201, {
                    {"message", "Registration successful"}
                });

            case RegisterResult::InvalidUsername:
                return make_error(400, "Invalid username");

            case RegisterResult::InvalidPassword:
                return make_error(400, "Invalid password");

            case RegisterResult::UserExists:
                return make_error(
                    409, "Username already exists"
                );

            case RegisterResult::InternalError:
                return make_error(
                    500, "Internal server error"
                );
        }

        return make_error(500, "Internal server error");
    }

    // 5. 未知路径
    return make_error(404, "Not Found");
}
