#pragma once

#include <array>
#include <chrono>
#include <map>
#include <mutex>
#include <string>

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

    // 根据 Token 获取经过验证的用户名
    bool authenticate(
        const std::string& token,
        std::string& username
    );

    // 注销当前 Session
    bool logout_user(const std::string& token);

private:
    static constexpr int SALT_SIZE = 16;
    static constexpr int HASH_SIZE = 32;

    static constexpr int PBKDF2_ITERATIONS = 600000;

    static constexpr int SESSION_MINUTES = 30;

    struct Account {
        std::array<unsigned char, SALT_SIZE> salt{};
        std::array<unsigned char, HASH_SIZE> password_hash{};
    };

    struct Session {
        std::string username;
        std::chrono::steady_clock::time_point expires_at;
    };

    // 用户名 -> 账户信息
    std::map<std::string, Account> accounts_;

    // Token -> Session 信息
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
};
