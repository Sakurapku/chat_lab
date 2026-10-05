#include "server/user_table.hpp"

#include <algorithm>
#include <utility>

bool UserTable::add(std::string username, int socket_fd) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (fd_by_username_.find(username) != fd_by_username_.end()) {
        return false;
    }

    fd_by_username_.emplace(username, socket_fd);
    username_by_fd_.emplace(socket_fd, std::move(username));
    return true;
}

std::optional<int> UserTable::find_fd(const std::string& username) const {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = fd_by_username_.find(username);
    if (it == fd_by_username_.end()) {
        return std::nullopt;
    }

    return it->second;
}

std::optional<std::string> UserTable::remove_by_fd(int socket_fd) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto fd_it = username_by_fd_.find(socket_fd);
    if (fd_it == username_by_fd_.end()) {
        return std::nullopt;
    }

    const std::string username = fd_it->second;

    username_by_fd_.erase(fd_it);
    fd_by_username_.erase(username);

    return username;
}

std::vector<std::string> UserTable::usernames() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> result;
    result.reserve(fd_by_username_.size());

    for (const auto& [username, socket_fd] : fd_by_username_) {
        (void)socket_fd;
        result.push_back(username);
    }

    std::sort(result.begin(), result.end());
    return result;
}
