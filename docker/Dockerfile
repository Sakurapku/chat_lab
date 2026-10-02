FROM debian:trixie

# 替换为北大镜像源
RUN sed -i 's/deb.debian.org/mirrors.pku.edu.cn/g' /etc/apt/sources.list.d/debian.sources && \
    sed -i 's/security.debian.org/mirrors.pku.edu.cn/g' /etc/apt/sources.list.d/debian.sources

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential clang cmake ninja-build gdb valgrind \
    libssl-dev nlohmann-json3-dev tcpdump iproute2 netcat-openbsd curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
CMD ["sleep", "infinity"]

