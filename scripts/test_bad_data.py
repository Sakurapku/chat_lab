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


def send_raw_frame(sock,
                   message_type,
                   payload,
                   magic=b"CL",
                   version=1,
                   declared_length=None):
    if declared_length is None:
        declared_length = len(payload)

    header = (
        magic +
        bytes([version, message_type]) +
        struct.pack("!I", declared_length)
    )

    sock.sendall(header + payload)


def recv_frame(sock):
    header = recv_exact(sock, 8)

    if header[0:2] != b"CL":
        raise RuntimeError("invalid magic in response")

    if header[2] != 1:
        raise RuntimeError("invalid version in response")

    message_type = header[3]
    length = struct.unpack("!I", header[4:8])[0]
    payload = recv_exact(sock, length)

    return message_type, json.loads(payload.decode("utf-8"))


def login(username):
    sock = socket.create_connection((HOST, PORT), timeout=3)
    payload = json.dumps({"username": username}).encode("utf-8")
    send_raw_frame(sock, 1, payload)

    message_type, response = recv_frame(sock)

    if message_type != 101 or response.get("ok") is not True:
        raise RuntimeError(f"login failed: type={message_type}, response={response}")

    return sock


def expect_error(name,
                 message_type,
                 payload,
                 expected_code,
                 magic=b"CL",
                 version=1,
                 declared_length=None,
                 need_login=False):
    if need_login:
        sock = login("bad_data_tester")
    else:
        sock = socket.create_connection((HOST, PORT), timeout=3)

    send_raw_frame(
        sock,
        message_type,
        payload,
        magic=magic,
        version=version,
        declared_length=declared_length,
    )

    response_type, response = recv_frame(sock)

    assert response_type == 104, (name, response_type, response)
    assert response["code"] == expected_code, (name, response)

    sock.close()
    print(f"[PASS] {name}：返回 {expected_code}")


def test_incomplete_payload():
    sock = socket.create_connection((HOST, PORT), timeout=3)

    # 声明负载有 10 字节，但实际只发送 3 字节，然后直接关闭连接。
    send_raw_frame(
        sock,
        1,
        b"abc",
        declared_length=10,
    )
    sock.close()

    # 服务端不应崩溃。随后重新登录一个用户，确认服务端仍然可用。
    tester = login("still_alive")
    tester.close()

    print("[PASS] 不完整负载：服务端没有崩溃")


def main():
    expect_error(
        name="错误魔数",
        message_type=1,
        payload=json.dumps({"username": "alice"}).encode("utf-8"),
        expected_code="PROTOCOL_ERROR",
        magic=b"XX",
    )

    expect_error(
        name="错误版本",
        message_type=1,
        payload=json.dumps({"username": "alice"}).encode("utf-8"),
        expected_code="PROTOCOL_ERROR",
        version=2,
    )

    expect_error(
        name="超过协议层长度上限",
        message_type=1,
        payload=b"",
        expected_code="PROTOCOL_ERROR",
        declared_length=4 * 1024 * 1024 + 1,
    )

    expect_error(
        name="非法 JSON",
        message_type=1,
        payload=b"this is not json",
        expected_code="BAD_REQUEST",
    )

    expect_error(
        name="未知消息类型",
        message_type=200,
        payload=b"{}",
        expected_code="UNKNOWN_TYPE",
        need_login=True,
    )

    test_incomplete_payload()

    tester = login("final_tester")
    send_raw_frame(tester, 2, b"{}")
    message_type, response = recv_frame(tester)

    assert message_type == 102
    assert "final_tester" in response["users"]

    tester.close()

    print("[PASS] 异常测试结束后服务端仍然正常")
    print("所有异常数据测试通过")


if __name__ == "__main__":
    main()
