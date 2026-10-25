#include "web/auth/account.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

// 判断用户名是否出现在在线列表中
bool contains_user(
    const std::vector<std::string>& users,
    const std::string& username
) {
    return std::find(
        users.begin(), users.end(), username
    ) != users.end();
}

int main() {
    AccountStore accounts;

    // 1. 注册 Alice
    assert(
        accounts.register_user("alice", "password123")
        == RegisterResult::Success
    );

    // 2. 重复注册失败
    assert(
        accounts.register_user("alice", "another123")
        == RegisterResult::UserExists
    );

    // 3. 无效用户名
    assert(
        accounts.register_user("a!", "password123")
        == RegisterResult::InvalidUsername
    );

    // 4. 密码太短
    assert(
        accounts.register_user("bob", "123")
        == RegisterResult::InvalidPassword
    );

    // 5. 注册 Bob
    assert(
        accounts.register_user("bob", "password456")
        == RegisterResult::Success
    );

    // 6. 初始没有在线用户
    auto users = accounts.get_online_users();
    assert(users.empty());

    // 7. Alice 登录
    std::string alice_token;

    assert(
        accounts.login_user(
            "alice", "password123", alice_token
        ) == LoginResult::Success
    );

    assert(alice_token.size() == 64);

    // 8. 登录后 Alice 在线
    users = accounts.get_online_users();

    assert(users.size() == 1);
    assert(contains_user(users, "alice"));

    // 9. 错误密码登录失败
    std::string bad_token;

    assert(
        accounts.login_user(
            "alice", "wrongpassword", bad_token
        ) == LoginResult::InvalidCredentials
    );

    assert(bad_token.empty());

    // 10. Bob 登录
    std::string bob_token;

    assert(
        accounts.login_user(
            "bob", "password456", bob_token
        ) == LoginResult::Success
    );

    // 11. 两人同时在线
    users = accounts.get_online_users();

    assert(users.size() == 2);
    assert(contains_user(users, "alice"));
    assert(contains_user(users, "bob"));

    // 12. 心跳成功
    assert(accounts.heartbeat(alice_token));
    assert(accounts.heartbeat(bob_token));

    // 13. 伪造 Token 心跳失败
    std::string fake_token(64, '0');

    assert(!accounts.heartbeat(fake_token));

    // 14. Alice 再次登录
    std::string alice_token2;

    assert(
        accounts.login_user(
            "alice", "password123", alice_token2
        ) == LoginResult::Success
    );

    assert(alice_token != alice_token2);

    // 同一用户多 Session，在线列表不重复
    users = accounts.get_online_users();

    assert(users.size() == 2);

    // 15. 第一个 Alice Session 退出
    assert(accounts.logout_user(alice_token));

    // Alice 另一个 Session 仍在线
    users = accounts.get_online_users();

    assert(users.size() == 2);
    assert(contains_user(users, "alice"));

    // 16. 第二个 Alice Session 退出
    assert(accounts.logout_user(alice_token2));

    users = accounts.get_online_users();

    assert(users.size() == 1);
    assert(!contains_user(users, "alice"));
    assert(contains_user(users, "bob"));

    // 17. 已注销 Token 不可用
    std::string username;

    assert(
        !accounts.authenticate(
            alice_token, username
        )
    );

    // 18. Bob 退出
    assert(accounts.logout_user(bob_token));

    users = accounts.get_online_users();

    assert(users.empty());

    std::cout << "All account tests passed!\n";

    return 0;
}
