#include "web/auth/account.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <cctype>
#include <limits>
#include <mutex>

namespace {

constexpr int PBKDF2_ITERATIONS = 600000;

// 校验用户名
bool valid_username(const std::string& username) {
    if (username.size() < 3 || username.size() > 32) {
        return false;
    }

    for (unsigned char c : username) {
        if (!std::isalnum(c) && c != '_') {
            return false;
        }

        // 本阶段只接受 ASCII 字母、数字和下划线
        if (c > 127) {
            return false;
        }
    }

    return true;
}

// 校验密码长度
bool valid_password(const std::string& password) {
    return password.size() >= 8 &&
           password.size() <= 128;
}

} // namespace

RegisterResult AccountStore::register_user(
    const std::string& username,
    const std::string& password
) {
    if (!valid_username(username)) {
        return RegisterResult::InvalidUsername;
    }

    if (!valid_password(password)) {
        return RegisterResult::InvalidPassword;
    }

    // 防止两个请求同时注册相同用户名
    std::lock_guard<std::mutex> lock(mutex_);

    if (accounts_.find(username) != accounts_.end()) {
        return RegisterResult::UserExists;
    }

    Account account{};

    // 生成随机盐值
    if (RAND_bytes(account.salt.data(), SALT_SIZE) != 1) {
        return RegisterResult::InternalError;
    }

    // 将密码转换为派生结果，不保存明文
    int ok = PKCS5_PBKDF2_HMAC(
        password.data(),
        static_cast<int>(password.size()),
        account.salt.data(),
        SALT_SIZE,
        PBKDF2_ITERATIONS,
        EVP_sha256(),
        HASH_SIZE,
        account.password_hash.data()
    );

    if (ok != 1) {
        return RegisterResult::InternalError;
    }

    accounts_.emplace(username, account);

    return RegisterResult::Success;
}

