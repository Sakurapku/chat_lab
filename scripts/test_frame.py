#!/usr/bin/env python3

import json
import socket
import struct
import sys


def recv_exact(sock, size):
    result = b""

    while len(result) < size:
        chunk = sock.recv(size - len(result))
        if not chunk:
            raise RuntimeError("connection closed")
        result += chunk

    return result


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 9000

    payload = json.dumps({"username": "alice"}).encode("utf-8")

    # MAGIC + VERSION + TYPE + LENGTH
    header = b"CL" + bytes([1, 1]) + struct.pack("!I", len(payload))

    with socket.create_connection((host, port)) as sock:
        sock.sendall(header + payload)

        response_header = recv_exact(sock, 8)
        magic = response_header[0:2]
        version = response_header[2]
        message_type = response_header[3]
        length = struct.unpack("!I", response_header[4:8])[0]

        if magic != b"CL":
            raise RuntimeError("invalid response magic")

        response_payload = recv_exact(sock, length)

        print("版本：", version)
        print("类型：", message_type)
        print("长度：", length)
        print("内容：", response_payload.decode("utf-8"))


if __name__ == "__main__":
    main()
