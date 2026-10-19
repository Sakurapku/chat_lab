#pragma once

#include <array>
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

class AccountStore {
public:
    RegisterResult register_user(
        const std::string& username,
        const std::string& password
    );

private:
    static constexpr int SALT_SIZE = 16;
    static constexpr int HASH_SIZE = 32;

    struct Account {
        std::array<unsigned char, SALT_SIZE> salt;
        std::array<unsigned char, HASH_SIZE> password_hash;
    };

    std::map<std::string, Account> accounts_;
    std::mutex mutex_;
};
