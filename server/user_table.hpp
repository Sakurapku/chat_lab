#ifndef CHAT_SERVER_USER_TABLE_HPP
#define CHAT_SERVER_USER_TABLE_HPP

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class UserTable {
public:
    // 添加在线用户。
    // 用户名已存在时返回 false，添加成功时返回 true。
    bool add(std::string username, int socket_fd);

    // 根据用户名查找 socket。
    std::optional<int> find_fd(const std::string& username) const;

    // 根据 socket 删除用户。
    // 如果用户存在，返回被删除的用户名。
    std::optional<std::string> remove_by_fd(int socket_fd);

    // 返回当前所有在线用户名。
    std::vector<std::string> usernames() const;

private:
    mutable std::mutex mutex_;

    std::unordered_map<std::string, int> fd_by_username_;
    std::unordered_map<int, std::string> username_by_fd_;
};

#endif  // CHAT_SERVER_USER_TABLE_HPP
