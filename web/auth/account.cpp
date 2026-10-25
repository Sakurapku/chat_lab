#include "web/auth/account.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <chrono>
#include <limits>
#include <mutex>
#include <string>

// 判断用户名是否合法
bool AccountStore::valid_username(
    const std::string& username
) {
    if (username.size() < 3 || username.size() > 32) {
        return false;
    }

    for (unsigned char c : username) {
        bool valid =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_';

        if (!valid) {
            return false;
        }
    }

    return true;
}

// 判断密码长度是否合法
bool AccountStore::valid_password(
    const std::string& password
) {
    return password.size() >= 8 &&
           password.size() <= 128;
}

// PBKDF2-HMAC-SHA256 密码派生
bool AccountStore::hash_password(
    const std::string& password,
    const std::array<unsigned char, SALT_SIZE>& salt,
    std::array<unsigned char, HASH_SIZE>& result
) {
    int ok = PKCS5_PBKDF2_HMAC(
        password.data(),
        static_cast<int>(password.size()),
        salt.data(),
        SALT_SIZE,
        PBKDF2_ITERATIONS,
        EVP_sha256(),
        HASH_SIZE,
        result.data()
    );

    return ok == 1;
}

// 生成 32 字节随机 Token，转换为 64 位十六进制字符
bool AccountStore::generate_token(std::string& token) {
    std::array<unsigned char, 32> random_bytes{};

    if (RAND_bytes(
            random_bytes.data(),
            static_cast<int>(random_bytes.size())
        ) != 1) {
        return false;
    }

    const char hex[] = "0123456789abcdef";

    std::string result;
    result.reserve(64);

    for (unsigned char byte : random_bytes) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 0x0f]);
    }

    token = std::move(result);
    return true;
}

// 注册账户
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

    std::lock_guard<std::mutex> lock(mutex_);

    if (accounts_.find(username) != accounts_.end()) {
        return RegisterResult::UserExists;
    }

    Account account{};

    if (RAND_bytes(account.salt.data(), SALT_SIZE) != 1) {
        return RegisterResult::InternalError;
    }

    if (!hash_password(
            password,
            account.salt,
            account.password_hash
        )) {
        return RegisterResult::InternalError;
    }

    accounts_.emplace(username, account);

    return RegisterResult::Success;
}

// 登录并生成 Session Token
LoginResult AccountStore::login_user(
    const std::string& username,
    const std::string& password,
    std::string& token
) {
    token.clear();

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = accounts_.find(username);

    if (it == accounts_.end()) {
        return LoginResult::InvalidCredentials;
    }

    const Account& account = it->second;

    // 使用原来的盐值计算输入密码的派生值
    std::array<unsigned char, HASH_SIZE> input_hash{};

    if (!hash_password(
            password,
            account.salt,
            input_hash
        )) {
        return LoginResult::InternalError;
    }

    // 常量时间比较，避免普通逐字节比较带来的时序泄露
    int equal = CRYPTO_memcmp(
        input_hash.data(),
        account.password_hash.data(),
        HASH_SIZE
    );

    OPENSSL_cleanse(input_hash.data(), input_hash.size());

    if (equal != 0) {
        return LoginResult::InvalidCredentials;
    }

    std::string new_token;

    if (!generate_token(new_token)) {
        return LoginResult::InternalError;
    }

    auto expires = std::chrono::steady_clock::now()
        + std::chrono::minutes(SESSION_MINUTES);

    // 极小概率下如果 Token 重复，则重新生成
    while (sessions_.find(new_token) != sessions_.end()) {
        if (!generate_token(new_token)) {
            return LoginResult::InternalError;
        }
    }

    sessions_.emplace(
        new_token,
        Session{username, expires}
    );

    token = std::move(new_token);

    return LoginResult::Success;
}

// 根据 Token 确认用户身份
bool AccountStore::authenticate(
    const std::string& token,
    std::string& username
) {
    username.clear();

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = sessions_.find(token);

    if (it == sessions_.end()) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();

    // 会话过期，立即删除
    if (now >= it->second.expires_at) {
        sessions_.erase(it);
        return false;
    }

    username = it->second.username;

    return true;
}

// 注销用户，使 Token 立即失效
bool AccountStore::logout_user(
    const std::string& token
) {
    std::lock_guard<std::mutex> lock(mutex_);

    return sessions_.erase(token) > 0;
}
