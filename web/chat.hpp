#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

struct ChatMessage {
    std::uint64_t id = 0;
    std::string from;
    std::string to;
    std::string content;
    std::string type;
};

enum class SendResult {
    Success,
    EmptyMessage,
    TooLarge,
    UserOffline,
    QueueFull
};

class ChatStore {
public:
    static constexpr std::size_t MAX_MESSAGE_SIZE =
        1024 * 1024;

    static constexpr std::size_t MAX_QUEUE_BYTES =
        4 * 1024 * 1024;

    static constexpr std::size_t MAX_QUEUE_COUNT = 64;

    // to 为空表示群聊
    SendResult send_message(
        const std::string& from,
        const std::string& to,
        const std::string& content,
        const std::vector<std::string>& online_users,
        std::uint64_t& message_id
    ) {
        message_id = 0;

        if (content.empty()) {
            return SendResult::EmptyMessage;
        }

        if (content.size() > MAX_MESSAGE_SIZE) {
            return SendResult::TooLarge;
        }

        std::vector<std::string> recipients;

        if (to.empty()) {
            // 群聊：发送给所有在线用户
            recipients = online_users;
        } else {
            // 私聊：目标用户必须在线
            auto it = std::find(
                online_users.begin(),
                online_users.end(),
                to
            );

            if (it == online_users.end()) {
                return SendResult::UserOffline;
            }

            recipients.push_back(to);

            // 发送者也保留自己的消息副本
            if (from != to) {
                recipients.push_back(from);
            }
        }

        // 去重
        std::sort(recipients.begin(), recipients.end());

        recipients.erase(
            std::unique(
                recipients.begin(),
                recipients.end()
            ),
            recipients.end()
        );

        std::lock_guard<std::mutex> lock(mutex_);

        // 所有接收者的队列都需要有足够空间
        // 否则整条消息发送失败
        for (const auto& username : recipients) {
            auto it = queues_.find(username);

            if (it == queues_.end()) {
                continue;
            }

            if (it->second.size() >= MAX_QUEUE_COUNT) {
                return SendResult::QueueFull;
            }

            std::size_t current_bytes =
                queue_bytes_[username];

            if (current_bytes >
                MAX_QUEUE_BYTES - content.size()) {
                return SendResult::QueueFull;
            }
        }

        ChatMessage message;

        message.id = next_id_++;
        message.from = from;
        message.to = to;
        message.content = content;
        message.type = to.empty() ? "group" : "private";

        for (const auto& username : recipients) {
            queues_[username].push_back(message);
            queue_bytes_[username] += content.size();
        }

        message_id = message.id;

        return SendResult::Success;
    }

    // 读取并清空当前用户的待收消息
    std::vector<ChatMessage> take_messages(
        const std::string& username
    ) {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<ChatMessage> result;

        auto it = queues_.find(username);

        if (it == queues_.end()) {
            return result;
        }

        result.assign(
            it->second.begin(),
            it->second.end()
        );

        queues_.erase(it);
        queue_bytes_.erase(username);

        return result;
    }

private:
    std::map<std::string, std::deque<ChatMessage>> queues_;

    // 各用户队列中消息正文占用的字节数
    std::map<std::string, std::size_t> queue_bytes_;

    std::uint64_t next_id_ = 1;

    std::mutex mutex_;
};
