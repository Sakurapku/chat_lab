
cmake_minimum_required(VERSION 3.17)
project(chat_lab CXX)

# ==================== C++ 编译设置 ====================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-ferror-limit=5)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-fmax-errors=5)
endif()

# AddressSanitizer 和 UndefinedBehaviorSanitizer
option(ENABLE_ASAN "Enable AddressSanitizer and UBSan" OFF)

if(ENABLE_ASAN)
    add_compile_options(
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
        -g
    )
    add_link_options(-fsanitize=address,undefined)
endif()

# ==================== 查找依赖库 ====================

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json REQUIRED)

# ==================== 第一阶段：公共核心库 ====================

add_library(chat_core
    common/net.cpp
    common/protocol.cpp
)

target_include_directories(chat_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(chat_core PUBLIC
    Threads::Threads
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
)

# ==================== 第一阶段：TCP 服务端 ====================

add_executable(chat_server
    server/main.cpp
    server/user_table.cpp
)

target_link_libraries(chat_server PRIVATE
    chat_core
)

# ==================== 第一阶段：TCP 客户端 ====================

add_executable(chat_client
    client/main.cpp
)

target_link_libraries(chat_client PRIVATE
    chat_core
)

# ==================== 第二阶段：账户管理库 ====================

add_library(chat_account
    web/auth/account.cpp
)

target_include_directories(chat_account PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(chat_account PUBLIC
    OpenSSL::Crypto
    Threads::Threads
)

# ==================== 第二阶段：HTTP 服务端 ====================

add_executable(chat_web
    web/main.cpp
    web/http.cpp
    web/router.cpp
)

target_include_directories(chat_web PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(chat_web PRIVATE
    chat_account
    nlohmann_json::nlohmann_json
    Threads::Threads
)

# ==================== 单元测试 ====================

enable_testing()

# 第一阶段：协议测试
add_executable(protocol_test
    tests/protocol_test.cpp
)

target_link_libraries(protocol_test PRIVATE
    chat_core
)

add_test(NAME protocol_test COMMAND protocol_test)

# 第一阶段：网络通信测试
add_executable(net_test
    tests/net_test.cpp
)

target_link_libraries(net_test PRIVATE
    chat_core
)

add_test(NAME net_test COMMAND net_test)

# 第二阶段：HTTP 请求解析测试
add_executable(http_test
    tests/http_test.cpp
    web/http.cpp
)

target_include_directories(http_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_test(NAME http_test COMMAND http_test)

# 第二阶段：路由测试
add_executable(router_test
    tests/router_test.cpp
    web/router.cpp
)

target_include_directories(router_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(router_test PRIVATE
    chat_account
    nlohmann_json::nlohmann_json
)

add_test(NAME router_test COMMAND router_test)

# 第二阶段：用户账户测试
add_executable(account_test
    tests/account_test.cpp
)

target_link_libraries(account_test PRIVATE
    chat_account
)

add_test(NAME account_test COMMAND account_test)
