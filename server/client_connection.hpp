#ifndef CHAT_SERVER_CLIENT_CONNECTION_HPP
#define CHAT_SERVER_CLIENT_CONNECTION_HPP

#include <mutex>
#include <unistd.h>

class UniqueFd {
public:
    UniqueFd() = default;

    explicit UniqueFd(int fd)
        : fd_(fd) {}

    ~UniqueFd() {
        reset();
    }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept
        : fd_(other.release()) {}

    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    int get() const {
        return fd_;
    }

    explicit operator bool() const {
        return fd_ >= 0;
    }

    int release() {
        const int old_fd = fd_;
        fd_ = -1;
        return old_fd;
    }

    void reset(int new_fd = -1) {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = new_fd;
    }

private:
    int fd_ = -1;
};

class ClientConnection {
public:
    explicit ClientConnection(int socket_fd)
        : socket(socket_fd) {}

    int fd() const {
        return socket.get();
    }

    // socket 由 UniqueFd 自动关闭
    UniqueFd socket;

    // 所有线程向这个客户端发送数据前，都必须先获得这把锁
    std::mutex send_mutex;
};

#endif  // CHAT_SERVER_CLIENT_CONNECTION_HPP
