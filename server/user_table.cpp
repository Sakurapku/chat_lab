#include "server/user_table.hpp"

#include <algorithm>
#include <utility>

bool UserTable::add(
    std::string username,
    std::shared_ptr<ClientConnection> client) {
    if (!client || client->fd() < 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    const int socket_fd = client->fd();

    if (client_by_username_.find(username) !=
        client_by_username_.end()) {
        return false;
    }

    if (username_by_fd_.find(socket_fd) != username_by_fd_.end()) {
        return false;
    }

    client_by_username_.emplace(username, std::move(client));
    username_by_fd_.emplace(socket_fd, std::move(username));
    return true;
}

std::shared_ptr<ClientConnection> UserTable::find_client(
    const std::string& username) const {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = client_by_username_.find(username);
    if (it == client_by_username_.end()) {
        return nullptr;
    }

    return it->second;
}

std::optional<std::string> UserTable::username_of(
    int socket_fd) const {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = username_by_fd_.find(socket_fd);
    if (it == username_by_fd_.end()) {
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
    client_by_username_.erase(username);

    return username;
}

std::vector<std::string> UserTable::usernames() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> result;
    result.reserve(client_by_username_.size());

    for (const auto& [username, client] : client_by_username_) {
        (void)client;
        result.push_back(username);
    }

    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::shared_ptr<ClientConnection>>
UserTable::clients() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::shared_ptr<ClientConnection>> result;
    result.reserve(client_by_username_.size());

    for (const auto& [username, client] : client_by_username_) {
        (void)username;
        result.push_back(client);
    }

    return result;
}
