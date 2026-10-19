#pragma once

#include "web/http.hpp"
#include "web/auth/account.hpp"

HttpResponse route_request(
    const HttpRequest& request,
    AccountStore& accounts
);
