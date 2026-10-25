#include "web/router.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {

HttpResponse make_json_response(
    int status,
    const json& data
) {
    HttpResponse response;

    response.status = status;
    response.content_type = "application/json; charset=utf-8";
    response.body = data.dump();

    return response;
}

HttpResponse make_error(
    int status,
    const std::string& message
) {
    return make_json_response(status, {
        {"error", message}
    });
}

bool is_json_request(const HttpRequest& request) {
    auto it = request.headers.find("content-type");

    if (it == request.headers.end()) {
        return false;
    }

    std::string value = it->second;

    std::size_t pos = value.find(';');
    value = value.substr(0, pos);

    std::size_t first = value.find_first_not_of(" \t");
    std::size_t last = value.find_last_not_of(" \t");

    if (first == std::string::npos) {
        return false;
    }

    value = value.substr(first, last - first + 1);

    for (char& c : value) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }

    return value == "application/json";
}

bool parse_json_object(
    const HttpRequest& request,
    json& data
) {
    data = json::parse(
        request.body, nullptr, false
    );

    return !data.is_discarded() && data.is_object();
}

bool extract_token(
    const HttpRequest& request,
    std::string& token
) {
    auto it = request.headers.find("authorization");

    if (it == request.headers.end()) {
        return false;
    }

    const std::string prefix = "Bearer ";

    if (it->second.compare(
            0, prefix.size(), prefix
        ) != 0) {
        return false;
    }

    token = it->second.substr(prefix.size());

    if (token.size() != 64) {
        return false;
    }

    for (char c : token) {
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }

    return true;
}

} // namespace

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

    // 2. 健康检查
    if (request.path == "/api/health") {
        if (request.method != "GET") {
            return make_error(405, "Method Not Allowed");
        }

        return make_json_response(200, {
            {"status", "ok"},
            {"service", "chat_lab"}
        });
    }

    // 3. JSON 回显
    if (request.path == "/api/echo") {
        if (request.method != "POST") {
            return make_error(405, "Method Not Allowed");
        }

        if (!is_json_request(request)) {
            return make_error(
                400, "Content-Type must be application/json"
            );
        }

        json data;

        if (!parse_json_object(request, data)) {
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

        constexpr std::size_t MAX_MESSAGE_SIZE = 1024 * 1024;

        if (message.size() > MAX_MESSAGE_SIZE) {
            return make_error(413, "Message Too Large");
        }

        return make_json_response(200, {
            {"message", message}
        });
    }

    // 4. 用户注册
    if (request.path == "/api/register") {
        if (request.method != "POST") {
            return make_error(405, "Method Not Allowed");
        }

        if (!is_json_request(request)) {
            return make_error(
                400, "Content-Type must be application/json"
            );
        }

        json data;

        if (!parse_json_object(request, data)) {
            return make_error(400, "Invalid JSON");
        }

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

        RegisterResult result =
            accounts.register_user(username, password);

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
                return make_error(409, "Username already exists");

            case RegisterResult::InternalError:
                return make_error(500, "Internal server error");
        }

        return make_error(500, "Internal server error");
    }

    // 5. 用户登录
    if (request.path == "/api/login") {
        if (request.method != "POST") {
            return make_error(405, "Method Not Allowed");
        }

        if (!is_json_request(request)) {
            return make_error(
                400, "Content-Type must be application/json"
            );
        }

        json data;

        if (!parse_json_object(request, data)) {
            return make_error(400, "Invalid JSON");
        }

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

        std::string token;

        LoginResult result =
            accounts.login_user(username, password, token);

        switch (result) {
            case LoginResult::Success:
                return make_json_response(200, {
                    {"message", "Login successful"},
                    {"token", token},
                    {"expires_in", 1800}
                });

            case LoginResult::InvalidCredentials:
                return make_error(
                    401, "Invalid username or password"
                );

            case LoginResult::InternalError:
                return make_error(
                    500, "Internal server error"
                );
        }

        return make_error(500, "Internal server error");
    }

    // 6. 当前登录用户
    if (request.path == "/api/me") {
        if (request.method != "GET") {
            return make_error(405, "Method Not Allowed");
        }

        std::string token;

        if (!extract_token(request, token)) {
            return make_error(
                401, "Missing or invalid token"
            );
        }

        std::string username;

        if (!accounts.authenticate(token, username)) {
            return make_error(
                401, "Session expired or invalid"
            );
        }

        return make_json_response(200, {
            {"username", username}
        });
    }

    // 7. 心跳接口
    if (request.path == "/api/heartbeat") {
        if (request.method != "POST") {
            return make_error(405, "Method Not Allowed");
        }

        std::string token;

        if (!extract_token(request, token)) {
            return make_error(
                401, "Missing or invalid token"
            );
        }

        if (!accounts.heartbeat(token)) {
            return make_error(
                401, "Session expired or invalid"
            );
        }

        return make_json_response(200, {
            {"status", "ok"}
        });
    }

    // 8. 在线用户列表
    if (request.path == "/api/users") {
        if (request.method != "GET") {
            return make_error(405, "Method Not Allowed");
        }

        std::string token;

        if (!extract_token(request, token)) {
            return make_error(
                401, "Missing or invalid token"
            );
        }

        std::string username;

        if (!accounts.authenticate(token, username)) {
            return make_error(
                401, "Session expired or invalid"
            );
        }

        std::vector<std::string> users =
            accounts.get_online_users();

        return make_json_response(200, {
            {"users", users},
            {"count", users.size()}
        });
    }

    // 9. 用户退出
    if (request.path == "/api/logout") {
        if (request.method != "POST") {
            return make_error(405, "Method Not Allowed");
        }

        std::string token;

        if (!extract_token(request, token)) {
            return make_error(
                401, "Missing or invalid token"
            );
        }

        if (!accounts.logout_user(token)) {
            return make_error(
                401, "Session expired or invalid"
            );
        }

        return make_json_response(200, {
            {"message", "Logout successful"}
        });
    }

    return make_error(404, "Not Found");
}
