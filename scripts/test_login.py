#!/usr/bin/env python3

import json
import socket
import struct
import sys
import time


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
    sock = socket.create_connection((HOST, PORT), timeout=3)
    send_frame(sock, 1, {"username": username})
    message_type, payload = recv_frame(sock)

    if message_type != 101:
        raise RuntimeError(f"expected LOGIN_RESP, got type {message_type}")

    return sock, payload


def request_users(sock):
    send_frame(sock, 2, {})
    message_type, payload = recv_frame(sock)

    if message_type != 102:
        raise RuntimeError(f"expected USER_LIST_RESP, got type {message_type}")

    return set(payload["users"])


def main():
    alice, alice_response = connect("alice")
    assert alice_response["ok"] is True
    print("alice 登录成功")

    bob, bob_response = connect("bob")
    assert bob_response["ok"] is True
    print("bob 登录成功")

    duplicate, duplicate_response = connect("alice")
    assert duplicate_response["ok"] is False
    duplicate.close()
    print("重复用户名被拒绝")

    users = request_users(bob)
    assert users == {"alice", "bob"}, users
    print("在线列表正确：alice 和 bob 都在线")

    alice.close()

    for _ in range(20):
        users = request_users(bob)
        if users == {"bob"}:
            print("alice 断开后已从在线列表中删除")
            break
        time.sleep(0.1)
    else:
        raise RuntimeError(f"alice 断开后仍在线：{users}")

    bob.close()
    print("登录和在线列表测试通过")


if __name__ == "__main__":
    main()
