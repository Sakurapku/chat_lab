#include "common/protocol.hpp"

#include <arpa/inet.h>

#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void check(bool condition, const std::string& test_name) {
    if (condition) {
        std::cout << "[PASS] " << test_name << '\n';
    } else {
        std::cerr << "[FAIL] " << test_name << '\n';
        ++g_failures;
    }
}

void expect_protocol_error(const std::function<void()>& action,
                           const std::string& test_name) {
    try {
        action();

        std::cerr << "[FAIL] " << test_name
                  << "：没有抛出 ProtocolError\n";
        ++g_failures;
    } catch (const chat::ProtocolError&) {
        std::cout << "[PASS] " << test_name << '\n';
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << test_name
                  << "：抛出了错误类型的异常：" << error.what() << '\n';
        ++g_failures;
    }
}

std::string make_header(std::uint8_t type, std::uint32_t payload_size) {
    std::string header(chat::kHeaderSize, '\0');

    header[0] = chat::kMagic0;
    header[1] = chat::kMagic1;
    header[2] = static_cast<char>(chat::kProtocolVersion);
    header[3] = static_cast<char>(type);

    const std::uint32_t network_payload_size = htonl(payload_size);
    std::memcpy(&header[4], &network_payload_size,
                sizeof(network_payload_size));

    return header;
}

void test_encode_frame() {
    const std::string payload = R"({"username":"alice"})";
    const chat::Frame frame{chat::MessageType::kLoginRequest, payload};

    const std::string encoded = chat::encode_frame(frame);

    check(encoded.size() == chat::kHeaderSize + payload.size(),
          "编码后的总长度正确");

    check(encoded[0] == 'C' && encoded[1] == 'L',
          "编码后的魔数正确");

    check(static_cast<std::uint8_t>(encoded[2]) == chat::kProtocolVersion,
          "编码后的协议版本正确");

    check(static_cast<std::uint8_t>(encoded[3]) ==
              static_cast<std::uint8_t>(chat::MessageType::kLoginRequest),
          "编码后的消息类型正确");

    check(encoded.substr(chat::kHeaderSize) == payload,
          "编码后的 JSON 负载正确");
}

void test_decode_header() {
    const std::string payload = R"({"username":"alice"})";
    const chat::Frame frame{chat::MessageType::kLoginRequest, payload};
    const std::string encoded = chat::encode_frame(frame);

    const std::string header_bytes = encoded.substr(0, chat::kHeaderSize);
    const chat::Header header = chat::decode_header(header_bytes);

    check(header.type == chat::MessageType::kLoginRequest,
          "解析出的消息类型正确");

    check(header.payload_size == payload.size(),
          "解析出的负载长度正确");
}

void test_bad_magic() {
    std::string header = make_header(
        static_cast<std::uint8_t>(chat::MessageType::kLoginRequest), 0);

    header[0] = 'X';

    expect_protocol_error(
        [&]() {
            chat::decode_header(header);
        },
        "魔数错误时抛出 ProtocolError");
}

void test_bad_version() {
    std::string header = make_header(
        static_cast<std::uint8_t>(chat::MessageType::kLoginRequest), 0);

    header[2] = static_cast<char>(2);

    expect_protocol_error(
        [&]() {
            chat::decode_header(header);
        },
        "协议版本错误时抛出 ProtocolError");
}

void test_short_header() {
    expect_protocol_error(
        [&]() {
            chat::decode_header("CL");
        },
        "头部不足 8 字节时抛出 ProtocolError");
}

void test_encode_payload_too_large() {
    chat::Frame frame;
    frame.type = chat::MessageType::kSendChatRequest;
    frame.payload = std::string(chat::kMaxPayloadSize + 1, 'x');

    expect_protocol_error(
        [&]() {
            chat::encode_frame(frame);
        },
        "发送超过 4 MiB 的负载时抛出 ProtocolError");
}

void test_decode_payload_too_large() {
    const std::string header = make_header(
        static_cast<std::uint8_t>(chat::MessageType::kSendChatRequest),
        chat::kMaxPayloadSize + 1);

    expect_protocol_error(
        [&]() {
            chat::decode_header(header);
        },
        "接收超过 4 MiB 的负载时抛出 ProtocolError");
}

void test_unknown_message_type() {
    const std::string header = make_header(200, 0);
    const chat::Header decoded = chat::decode_header(header);

    check(static_cast<std::uint8_t>(decoded.type) == 200,
          "未知消息类型仍然可以被解析出来");

    check(decoded.payload_size == 0,
          "未知消息类型的负载长度正确");

    check(std::string(chat::to_string(decoded.type)) == "UNKNOWN",
          "未知消息类型可以转换成 UNKNOWN 字符串");
}

}  // namespace

int main() {
    test_encode_frame();
    test_decode_header();
    test_bad_magic();
    test_bad_version();
    test_short_header();
    test_encode_payload_too_large();
    test_decode_payload_too_large();
    test_unknown_message_type();

    if (g_failures != 0) {
        std::cerr << g_failures << " 个测试失败\n";
        return 1;
    }

    std::cout << "所有协议测试通过\n";
    return 0;
}
