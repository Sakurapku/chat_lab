#pragma once

#include <array>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <vector>

enum class RegisterResult {
    Success,
    InvalidUsername,
    InvalidPassword,
    UserExists,
    InternalError
};

enum class LoginResult {
    Success,
    InvalidCredentials,
    InternalError
};

class AccountStore {
public:
    RegisterResult register_user(
        const std::string& username,
        const std::string& password
    );

    LoginResult login_user(
        const std::string& username,
        const std::string& password,
        std::string& token
    );

    bool authenticate(
        const std::string& token,
        std::string& username
    );

    bool logout_user(const std::string& token);

    // 更新某个 Session 的最近活动时间
    bool heartbeat(const std::string& token);

    // 返回当前在线用户名列表
    std::vector<std::string> get_online_users();

private:
    static constexpr int SALT_SIZE = 16;
    static constexpr int HASH_SIZE = 32;
    static constexpr int PBKDF2_ITERATIONS = 600000;

    static constexpr int SESSION_MINUTES = 30;
    static constexpr int ONLINE_TIMEOUT_SECONDS = 20;

    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    struct Account {
        std::array<unsigned char, SALT_SIZE> salt{};
        std::array<unsigned char, HASH_SIZE> password_hash{};
    };

    struct Session {
        std::string username;
        TimePoint expires_at;
        TimePoint last_active;
    };

    std::map<std::string, Account> accounts_;
    std::map<std::string, Session> sessions_;

    std::mutex mutex_;

    static bool valid_username(const std::string& username);
    static bool valid_password(const std::string& password);

    static bool hash_password(
        const std::string& password,
        const std::array<unsigned char, SALT_SIZE>& salt,
        std::array<unsigned char, HASH_SIZE>& result
    );

    static bool generate_token(std::string& token);

    // 必须在持有 mutex_ 时调用
    void remove_expired_sessions(TimePoint now);
};
