#pragma once

#include "web/http.hpp"
#include "web/auth/account.hpp"
#include "web/chat.hpp"

// 根据 HTTP 请求生成响应
HttpResponse route_request(
    const HttpRequest& request,
    AccountStore& accounts,
    ChatStore& chats
);
