#include "web/auth/account.hpp"

#include <cassert>
#include <iostream>
#include <string>

int main() {
    AccountStore accounts;

    // 测试 1：正常注册
    assert(
        accounts.register_user("alice", "password123")
        == RegisterResult::Success
    );

    // 测试 2：重复注册
    assert(
        accounts.register_user("alice", "another123")
        == RegisterResult::UserExists
    );

    // 测试 3：非法用户名
    assert(
        accounts.register_user("a!", "password123")
        == RegisterResult::InvalidUsername
    );

    // 测试 4：密码太短
    assert(
        accounts.register_user("bob", "123")
        == RegisterResult::InvalidPassword
    );

    // 测试 5：正确密码登录
    std::string token;

    assert(
        accounts.login_user(
            "alice", "password123", token
        ) == LoginResult::Success
    );

    assert(token.size() == 64);

    // 测试 6：错误密码无法登录
    std::string bad_token;

    assert(
        accounts.login_user(
            "alice", "wrongpassword", bad_token
        ) == LoginResult::InvalidCredentials
    );

    assert(bad_token.empty());

    // 测试 7：不存在的用户无法登录
    assert(
        accounts.login_user(
            "nobody", "password123", bad_token
        ) == LoginResult::InvalidCredentials
    );

    // 测试 8：正确 Token 能取得用户名
    std::string username;

    assert(accounts.authenticate(token, username));
    assert(username == "alice");

    // 测试 9：伪造 Token 无法通过认证
    std::string fake_token(64, '0');

    assert(
        !accounts.authenticate(fake_token, username)
    );

    // 测试 10：两次登录产生不同 Token
    std::string token2;

    assert(
        accounts.login_user(
            "alice", "password123", token2
        ) == LoginResult::Success
    );

    assert(token != token2);

    // 测试 11：注销第一个 Token
    assert(accounts.logout_user(token));

    // 测试 12：注销后第一个 Token 无效
    assert(
        !accounts.authenticate(token, username)
    );

    // 测试 13：第二个 Token 仍然有效
    assert(
        accounts.authenticate(token2, username)
    );

    assert(username == "alice");

    // 测试 14：第二个 Token 注销
    assert(accounts.logout_user(token2));

    assert(
        !accounts.authenticate(token2, username)
    );

    std::cout << "All account tests passed!\n";

    return 0;
}
