#ifndef CHAT_SERVER_USER_TABLE_HPP
#define CHAT_SERVER_USER_TABLE_HPP

#include "server/client_connection.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class UserTable {
public:
    // 添加在线用户。
    // 用户名或 socket 已存在时返回 false。
    bool add(std::string username,
             std::shared_ptr<ClientConnection> client);

    // 根据用户名查找完整客户端连接。
    std::shared_ptr<ClientConnection> find_client(
        const std::string& username) const;

    // 根据 socket 查找用户名。
    std::optional<std::string> username_of(int socket_fd) const;

    // 根据 socket 删除用户。
    std::optional<std::string> remove_by_fd(int socket_fd);

    // 返回当前所有在线用户名。
    std::vector<std::string> usernames() const;

    // 返回当前所有在线客户端。
    std::vector<std::shared_ptr<ClientConnection>> clients() const;

private:
    mutable std::mutex mutex_;

    std::unordered_map<std::string,
                       std::shared_ptr<ClientConnection>>
        client_by_username_;

    std::unordered_map<int, std::string> username_by_fd_;
};

#endif  // CHAT_SERVER_USER_TABLE_HPP
