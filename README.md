# chat_lab

使用 C++17 从零实现的多人聊天软件。

当前已完成阶段一：命令行聊天。

## 当前功能

- 客户端使用用户名登录；
- 重复用户名会被拒绝；
- 查看当前在线用户列表；
- 群发消息；
- 私聊消息；
- 私聊目标不存在时返回明确错误；
- 单条聊天内容最大支持 1 MiB；
- 超过 1 MiB 的聊天内容会被拒绝；
- 协议层单个 JSON 负载最大为 4 MiB；
- 一条消息分多次到达时仍能正确还原；
- 多条消息粘在一起时能正确拆分；
- 错误魔数、错误版本、非法长度和非法 JSON 不会导致服务端崩溃；
- 客户端退出或被 `docker kill` 后，服务端会清理在线用户；
- 服务端使用“一连接一线程”；
- 每个客户端连接有独立发送锁，避免多个线程同时写同一个 socket。

## 技术栈

- C++17
- CMake
- Ninja
- clang++
- nlohmann-json
- Linux socket API
- Docker
- Python 3 集成测试脚本

## 项目结构

```text
chat_lab/
├── client/
│   └── main.cpp             # 命令行客户端
├── common/
│   ├── net.cpp              # 完整发送、完整接收、帧读写
│   ├── net.hpp
│   ├── protocol.cpp         # 帧编解码
│   └── protocol.hpp
├── docs/
│   ├── design.md            # 设计说明
│   └── protocol.md          # 通信协议
├── scripts/
│   ├── client.sh            # 启动 Docker 客户端
│   ├── test_bad_data.py     # 异常协议数据测试
│   ├── test_chat.py         # 登录、私聊、群发和大消息测试
│   ├── test_frame.py        # 最基础的协议帧测试
│   └── test_login.py        # 登录和在线列表测试
├── server/
│   ├── client_connection.hpp
│   ├── main.cpp             # 服务端主程序
│   ├── user_table.cpp       # 在线用户表
│   └── user_table.hpp
├── tests/
│   ├── net_test.cpp         # 网络收发、拆包和粘包测试
│   └── protocol_test.cpp    # 协议编解码测试
├── CMakeLists.txt
└── README.md
```

## 编译

安装依赖：

```bash
sudo apt update
sudo apt install -y git build-essential clang cmake ninja-build gdb valgrind \
                    libssl-dev nlohmann-json3-dev tcpdump iproute2 \
                    netcat-openbsd curl docker.io
```

配置并编译：

```bash
cd ~/chat_lab

cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_COMPILER=clang \
      -DCMAKE_CXX_COMPILER=clang++

cmake --build build -j
```

## 启动服务端

```bash
cd ~/chat_lab
./build/chat_server 9000
```

服务端启动后会显示：

```text
chat server listening on port 9000
```

## 启动本地客户端

打开另一个终端：

```bash
cd ~/chat_lab
./build/chat_client 127.0.0.1 9000 alice
```

再打开一个终端：

```bash
cd ~/chat_lab
./build/chat_client 127.0.0.1 9000 bob
```

命令格式：

```text
./build/chat_client [主机] [端口] [用户名]
```

如果不提供用户名，客户端会提示输入用户名。

## 使用 Docker 客户端

第一次需要构建镜像：

```bash
cd ~/chat_lab
docker build -t chatlab:dev docker/
docker network create chatnet
```

启动客户端容器：

```bash
./scripts/client.sh host.docker.internal 9000
```

进入客户端后输入用户名。

需要两个客户端时，在两个不同终端中分别执行上面的命令。

## 客户端命令

```text
/help              显示帮助
/list              查看在线用户
/all 内容          群发消息
/msg 用户名 内容   私聊消息
/quit              退出客户端
```

直接输入普通文字时，默认作为群发消息发送。

## 自动化测试

运行协议和网络单元测试：

```bash
cd ~/chat_lab
ctest --test-dir build --output-on-failure
```

也可以直接运行：

```bash
./build/protocol_test
./build/net_test
```

## 集成测试

集成测试需要服务端已经启动。

第一个终端：

```bash
cd ~/chat_lab
./build/chat_server 9000
```

第二个终端运行：

```bash
python3 scripts/test_login.py 127.0.0.1 9000
python3 scripts/test_chat.py 127.0.0.1 9000
python3 scripts/test_bad_data.py 127.0.0.1 9000
```

注意：

- `test_login.py` 会使用 `alice` 和 `bob`；
- `test_chat.py` 会使用 `alice`、`bob` 和 `carol`；
- 如果这些用户名仍然在线，测试会失败；
- 重新运行测试前，建议先重启服务端。

## Docker 掉线测试

启动服务端和两个 Docker 客户端后，查看容器：

```bash
docker ps --filter ancestor=chatlab:dev
```

终止其中一个客户端容器：

```bash
docker kill &lt;容器ID&gt;
```

然后在另一个客户端中输入：

```text
/list
```

被终止的用户应该在 5 秒内从在线列表中消失。

## 协议说明

详细协议见：

```text
docs/protocol.md
```

当前协议使用：

```text
固定 8 字节头部 + UTF-8 JSON 负载
```

头部包括：

- 2 字节魔数 `CL`；
- 1 字节版本；
- 1 字节消息类型；
- 4 字节网络大端序负载长度。

## 当前限制

阶段一暂不支持：

- 用户密码；
- 加密传输；
- 历史消息；
- 离线消息；
- 心跳超时；
- 文件传输。

`docker pause` 或客户端断网时，TCP 连接可能不会立刻关闭。当前版本没有实现心跳，因此这种情况不一定能立即发现。
