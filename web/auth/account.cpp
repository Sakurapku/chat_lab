#include "web/auth/account.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <chrono>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

// 检查用户名是否合法
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

// 检查密码是否合法
bool AccountStore::valid_password(
    const std::string& password
) {
    return password.size() >= 8 &&
           password.size() <= 128;
}

// 使用 PBKDF2-HMAC-SHA256 派生密码
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

// 生成随机 Token
bool AccountStore::generate_token(
    std::string& token
) {
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

// 注册用户
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

// 登录用户
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

    std::array<unsigned char, HASH_SIZE> input_hash{};

    if (!hash_password(
            password,
            account.salt,
            input_hash
        )) {
        return LoginResult::InternalError;
    }

    int equal = CRYPTO_memcmp(
        input_hash.data(),
        account.password_hash.data(),
        HASH_SIZE
    );

    OPENSSL_cleanse(
        input_hash.data(),
        input_hash.size()
    );

    if (equal != 0) {
        return LoginResult::InvalidCredentials;
    }

    std::string new_token;

    if (!generate_token(new_token)) {
        return LoginResult::InternalError;
    }

    while (sessions_.find(new_token) != sessions_.end()) {
        if (!generate_token(new_token)) {
            return LoginResult::InternalError;
        }
    }

    TimePoint now = Clock::now();

    Session session{
        username,
        now + std::chrono::minutes(SESSION_MINUTES),
        now
    };

    sessions_.emplace(new_token, session);

    token = std::move(new_token);

    return LoginResult::Success;
}

// 清理超过 30 分钟的 Session
// 调用者必须已经持有 mutex_
void AccountStore::remove_expired_sessions(
    TimePoint now
) {
    for (auto it = sessions_.begin();
         it != sessions_.end();) {

        if (now >= it->second.expires_at) {
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

// 检查 Token 并确认用户名
bool AccountStore::authenticate(
    const std::string& token,
    std::string& username
) {
    username.clear();

    std::lock_guard<std::mutex> lock(mutex_);

    TimePoint now = Clock::now();

    remove_expired_sessions(now);

    auto it = sessions_.find(token);

    if (it == sessions_.end()) {
        return false;
    }

    username = it->second.username;

    // 经过验证的请求属于用户活动
    it->second.last_active = now;

    return true;
}

// 用户主动退出
bool AccountStore::logout_user(
    const std::string& token
) {
    std::lock_guard<std::mutex> lock(mutex_);

    remove_expired_sessions(Clock::now());

    return sessions_.erase(token) > 0;
}

// 处理心跳请求
bool AccountStore::heartbeat(
    const std::string& token
) {
    std::lock_guard<std::mutex> lock(mutex_);

    TimePoint now = Clock::now();

    remove_expired_sessions(now);

    auto it = sessions_.find(token);

    if (it == sessions_.end()) {
        return false;
    }

    it->second.last_active = now;

    return true;
}

// 获取在线用户名列表
std::vector<std::string> AccountStore::get_online_users() {
    std::lock_guard<std::mutex> lock(mutex_);

    TimePoint now = Clock::now();

    remove_expired_sessions(now);

    // set 自动去重且按用户名排序
    std::set<std::string> online;

    for (const auto& item : sessions_) {
        const Session& session = item.second;

        auto inactive_time = now - session.last_active;

        if (inactive_time <
            std::chrono::seconds(ONLINE_TIMEOUT_SECONDS)) {
            online.insert(session.username);
        }
    }

    return std::vector<std::string>(
        online.begin(), online.end()
    );
}
