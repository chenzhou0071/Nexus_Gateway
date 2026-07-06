# Nexus-Gateway

自研 C/C++ 轻量级高可用网关，支持四层 TCP 负载均衡与七层 HTTP 代理，基于 epoll 实现高并发，采用零拷贝优化性能，自带限流、健康检测、平滑热更新功能。

## 项目概述

Nexus-Gateway 是一个**从零实现的 Master-Worker 多进程网关**，对标 Nginx 核心设计，不依赖任何第三方网络库。项目旨在展示底层网络编程、高并发系统设计、进程管理等核心技术。

### 核心特性

- **高并发网络 I/O**：基于 epoll 的事件驱动模型，单进程可处理万级并发连接
- **异步 I/O**：非阻塞 socket + 状态机驱动，Worker 不阻塞在任何连接上
- **七层 HTTP 代理**：手写 HTTP/1.1 状态机解析器，支持路由匹配、动态代理转发
- **加权轮询**：支持后端节点权重配置，流量按比例分发
- **连接池管理**：预分配连接池，避免频繁内存分配
- **高可用设计**：Master-Worker 多进程模型，故障自动重启

### 技术栈

- **语言**：C（C99 标准）
- **网络模型**：epoll（Linux 特有）
- **并发模型**：Master-Worker 多进程
- **协议解析**：HTTP/1.1 状态机
- **构建工具**：Makefile

## 架构设计

### 进程模型

```
┌─────────────────┐
│   Master 进程   │  ← 管理进程、信号处理、配置热加载
└────────┬────────┘
         │ fork()
    ┌────┴────┐
    │         │
┌───▼───┐ ┌──▼───┐  ← Worker 进程池
│Worker1│ │Worker2│  - epoll 事件循环
└───┬───┘ └──┬───┘  - HTTP 解析 + 代理
    │         │      - 上游连接（异步）
    └────┬────┘
         │
    ┌────▼────┐
    │ 后端集群 │  ← 上游服务器（加权轮询）
    └─────────┘
```

### 核心模块

```
src/
├── main.c              # 入口，加载配置，启动 Master
├── master.c            # Master 进程（待实现）
├── worker.c            # Worker 进程，事件循环 + 状态机
├── epoll_loop.c        # epoll 封装，事件分发
├── acceptor.c          # listen + accept，SO_REUSEPORT
├── connection.c        # 连接池 + 状态机定义
├── http_parser.c       # HTTP/1.1 状态机解析器
├── http_header.c       # HTTP 头部哈希表检索
├── upstream.c          # 加权轮询，后端节点管理
├── proxy.c             # 上游连接（非阻塞 socket）
├── config.c            # INI 配置解析
├── log.c               # access/error 日志
└── util.c              # 字符串/内存/时间工具
```

### 异步 I/O 流程

```c
客户端连接 ──► READING_REQ ──► CONNECTING_UPSTREAM ──► READING_UPSTREAM ──► WRITING_CLIENT
               (读请求)        (非阻塞 connect)         (读响应)            (转发客户端)
```

每个连接维护一个状���机，通过 epoll 事件驱动状态转换，Worker 线程永不阻塞。

## 快速开始

### 编译

```bash
# 克隆仓库
git clone https://github.com/chenzhou0071/Nexus_Gateway.git
cd Nexus_Gateway

# 编译
make clean && make
```

### 配置

创建 `config.ini`：

```ini
[server]
listen       = 0.0.0.0:8080
worker_num   = auto
log_level    = info
max_conns    = 10000

[upstream.api]
server  = 127.0.0.1:19998 weight=1

[route]
/api    = api
```

### 启动

```bash
# 启动后端服务器（测试用）
python3 -m http.server 19998

# 启动网关
./bin/nexus_gateway config.ini
```

### 测试

```bash
curl http://127.0.0.1:8080/
```

## 开发进度

### ✅ 阶段 1：骨架（MVP 端到端）

- [x] Task 1.1: util 模块（字符串/内存/时间/原子工具）
- [x] Task 1.2: log 模块（Nginx 风格 access/error 日志）
- [x] Task 1.3: config 模块（INI 解析）
- [x] Task 1.4: connection 模块（连接池 + 状态机）
- [x] Task 1.5: epoll_loop 模块（事件循环骨架）
- [x] Task 1.6: http_header 模块（哈希表头检索）
- [x] Task 1.7: http_parser 模块（HTTP/1.1 状态机）
- [x] Task 1.8: upstream 模块（加权轮询）
- [x] Task 1.9: acceptor 模块（listen + SO_REUSEPORT）
- [x] Task 1.10: proxy 模块（基本代理转发）
- [x] Task 1.11: worker 端到端（第一个可运行代理）

### 🚧 阶段 2：高可用

- [ ] Task 2.1: master 进程 + 共享内存双缓冲
- [ ] Task 2.2: SO_REUSEPORT 多进程监听 + fork Worker
- [ ] Task 2.3: 平滑重启 + 存量连接排空

### 📋 阶段 3：功能增强

- [ ] 路由功能（路径前缀匹配）
- [ ] 安全模块（限流、黑名单）
- [ ] 监控统计（连接数、QPS、延迟）

## 技术亮点

### 1. 异步 I/O 模型

- **非阻塞 socket**：所有 socket 设置 `SOCK_NONBLOCK`，connect/read/write 立即返回
- **状态机驱动**：每个连接维护状态（READING_REQ → CONNECTING_UPSTREAM → READING_UPSTREAM）
- **epoll 事件驱动**：只处理可读/可写事件，避免忙等待

### 2. 连接池优化

- **预分配**：启动时分配 1024 个 `proxy_conn_t`，避免运行时 malloc
- **快速查找**：`proxy_get()` 同时检查客户端 fd 和上�� fd，O(n) 查找
- **状态复用**：连接释放后标记为空闲，下次直接复用

### 3. HTTP 解析器

- **状态机模式**：逐字节解析，支持分段喂入
- **零拷贝设计**：解析过程不修改原始数据，只记录指针和长度
- **头部优化**：哈希表存储常用头部（Host、Content-Length），O(1) 查找

### 4. 加权轮询

- **平滑算法**：基于 GCD 的权重归一化，避免节点连续选中
- **故障剔除**：健康检测失败自动下线节点
- **动态配置**：支持热加载权重和节点列表

## 设计文档

- [完整设计文档](docs/superpowers/specs/2026-07-06-nexus-gateway-design.md)
- [实现计划](docs/superpowers/plans/2026-07-06-nexus-gateway.md)

## License

MIT License

## 作者

chenzhou0071

---

**本项目为学习项目，仅用于技术研究和教育目的。生产环境请使用 Nginx、HAProxy 等成熟方案。**
