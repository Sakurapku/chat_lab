#include "web/router.hpp"

#include <nlohmann/json.hpp>

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
HttpResponse route_request(const HttpRequest& request) {

    // 1. 原有首页
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

        // 检查 Content-Type
        auto it = request.headers.find("content-type");

        if (it == request.headers.end()) {
            return make_error(400, "Missing Content-Type");
        }

        // 本阶段只接受 application/json
        std::string content_type = it->second;
        std::size_t semicolon = content_type.find(';');

        content_type = content_type.substr(0, semicolon);

        // 允许媒体类型前后有空格
        const std::size_t first =
            content_type.find_first_not_of(" \t");
        const std::size_t last =
            content_type.find_last_not_of(" \t");

        if (first == std::string::npos) {
            return make_error(400, "Invalid Content-Type");
        }

        content_type = content_type.substr(first, last - first + 1);

        if (content_type != "application/json") {
            return make_error(400, "Content-Type must be application/json");
        }

        // 解析 JSON，非法格式不会直接抛异常
        json data = json::parse(
            request.body, nullptr, false
        );

        if (data.is_discarded() || !data.is_object()) {
            return make_error(400, "Invalid JSON object");
        }

        // 必须存在 message 字段，且类型必须是字符串
        if (!data.contains("message") ||
            !data["message"].is_string()) {
            return make_error(400, "Missing or invalid message");
        }

        std::string message = data["message"].get<std::string>();

        // 暂定单条消息的业务上限为 1 MiB
        constexpr std::size_t MAX_MESSAGE_SIZE = 1024 * 1024;

        if (message.size() > MAX_MESSAGE_SIZE) {
            return make_error(413, "Message Too Large");
        }

        return make_json_response(200, {
            {"message", message}
        });
    }

    // 4. 未知路径
    return make_error(404, "Not Found");
}
