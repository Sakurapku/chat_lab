#!/usr/bin/env python3

import json
import socket
import struct
import sys


HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 9000


def recv_exact(sock, size):
    result = b""

    while len(result) < size:
        chunk = sock.recv(size - len(result))
        if not chunk:
            raise RuntimeError("connection closed")
        result += chunk

    return result


def send_frame(sock, message_type, payload):
    body = json.dumps(payload).encode("utf-8")
    header = b"CL" + bytes([1, message_type]) + struct.pack("!I", len(body))
    sock.sendall(header + body)


def recv_frame(sock):
    header = recv_exact(sock, 8)

    if header[0:2] != b"CL":
        raise RuntimeError("invalid magic")

    if header[2] != 1:
        raise RuntimeError("invalid version")

    message_type = header[3]
    length = struct.unpack("!I", header[4:8])[0]
    body = recv_exact(sock, length)

    return message_type, json.loads(body.decode("utf-8"))


def connect(username):
    sock = socket.create_connection((HOST, PORT), timeout=5)
    send_frame(sock, 1, {"username": username})
    message_type, payload = recv_frame(sock)

    if message_type != 101 or payload["ok"] is not True:
        raise RuntimeError(f"login failed: {payload}")

    return sock


def send_chat(sock, to, content):
    send_frame(sock, 3, {"to": to, "content": content})


def expect_no_frame(sock, name):
    old_timeout = sock.gettimeout()
    sock.settimeout(0.3)

    try:
        message_type, payload = recv_frame(sock)
        raise RuntimeError(
            f"{name} 不应该收到消息，但收到了："
            f"type={message_type}, payload={payload}"
        )
    except socket.timeout:
        pass
    finally:
        sock.settimeout(old_timeout)


def main():
    alice = connect("alice")
    bob = connect("bob")
    carol = connect("carol")

    print("三个用户登录成功")

    # 私聊：只有 bob 应该收到
    send_chat(alice, "bob", "hello bob")

    message_type, payload = recv_frame(bob)
    assert message_type == 103
    assert payload["from"] == "alice"
    assert payload["to"] == "bob"
    assert payload["content"] == "hello bob"

    expect_no_frame(carol, "carol")
    print("私聊测试通过")

    # 群发：三个人都应该收到
    send_chat(alice, "*", "hello everyone")

    for name, sock in [
        ("alice", alice),
        ("bob", bob),
        ("carol", carol),
    ]:
        message_type, payload = recv_frame(sock)
        assert message_type == 103
        assert payload["from"] == "alice"
        assert payload["to"] == "*"
        assert payload["content"] == "hello everyone"
        print(f"{name} 收到群发消息")

    print("群发测试通过")

    # 私聊目标不存在
    send_chat(alice, "nobody", "hello")
    message_type, payload = recv_frame(alice)

    assert message_type == 104
    assert payload["code"] == "USER_NOT_FOUND"
    print("目标不存在测试通过")

    # 1 MiB 消息必须可以传输
    large_content = "x" * (1024 * 1024)
    send_chat(alice, "bob", large_content)

    message_type, payload = recv_frame(bob)
    assert message_type == 103
    assert len(payload["content"].encode("utf-8")) == 1024 * 1024
    print("1 MiB 消息测试通过")

    # 超过 1 MiB 的消息必须被拒绝
    too_large_content = "x" * (1024 * 1024 + 1)
    send_chat(alice, "bob", too_large_content)

    message_type, payload = recv_frame(alice)
    assert message_type == 104
    assert payload["code"] == "MESSAGE_TOO_LARGE"
    print("超过 1 MiB 的消息被正确拒绝")

    alice.close()
    bob.close()
    carol.close()

    print("聊天测试全部通过")


if __name__ == "__main__":
    main()
