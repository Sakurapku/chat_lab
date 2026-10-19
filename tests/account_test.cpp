#include "web/auth/account.hpp"

#include <cassert>
#include <iostream>

int main() {
    AccountStore accounts;

    // 测试 1：正常注册
    assert(
        accounts.register_user("alice", "password123")
        == RegisterResult::Success
    );

    // 测试 2：重复用户名
    assert(
        accounts.register_user("alice", "another123")
        == RegisterResult::UserExists
    );

    // 测试 3：用户名太短
    assert(
        accounts.register_user("ab", "password123")
        == RegisterResult::InvalidUsername
    );

    // 测试 4：用户名含非法字符
    assert(
        accounts.register_user("alice!", "password123")
        == RegisterResult::InvalidUsername
    );

    // 测试 5：密码太短
    assert(
        accounts.register_user("bob", "123")
        == RegisterResult::InvalidPassword
    );

    // 测试 6：另一个用户正常注册
    assert(
        accounts.register_user("bob", "password456")
        == RegisterResult::Success
    );

    std::cout << "All account tests passed!\n";

    return 0;
}
