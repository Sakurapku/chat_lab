#pragma once

#include "web/http.hpp"

// 根据 HTTP 请求生成对应的响应
HttpResponse route_request(const HttpRequest& request);
