# Nexus-Gateway

自研 C/C++ 轻量级高可用网关，支持四层 TCP 负载均衡与七层 HTTP 代理，基于 epoll 实现高并发，采用零拷贝优化性能，自带限流、健康检测、平滑热更新功能。

## 项目概述

Nexus-Gateway 是一个**从零实现的 Master-Worker 多进程网关**，对标 Nginx 核心设计，不依赖任何第三方网络库。项目旨在展示底层网络编程、高并发系统设计、进程管理等核心技术。

### 核心特性

- **高并发网络 I/O**：基于 epoll 的事件驱动模型，单进程可处理万级并发连接
- **异步 I/O**：非阻塞 socket + 状态机驱动，Worker 不阻塞在任何连接上
- **七层 HTTP 代理**：手写 HTTP/1.1 状态机解析器，支持路由匹配、动态代理转发
- **HTTP 头部改写**：符合 RFC 7239 标准的代理头部注入（X-Real-IP、X-Forwarded-For、X-Forwarded-Host、X-Forwarded-Proto、Via）
- **请求追踪 ID**：全局唯一的 Req-ID（Base36 编码），便于分布式追踪
- **完整访问日志**：Nginx 风格的访问日志，包含客户端地址、请求行、状态码、耗时、Req-ID 等
- **加权轮询**：支持后端节点权重配置，流量按比例分发
- **连接池管理**：预分配连接池，避免频繁内存分配
- **Master-Worker 架构**：1 个 Master 管理 N 个 Worker，支持平滑重启和配置热更新
- **平滑重启**：SIGHUP 触发，旧 Worker 排空存量连接后退出，新 Worker 无缝接管
- **Generation 机制**：用计数器区分重启轮次，支持多轮连续热启动
- **零停机时间**：配置热更新，连接不丢失，实测 `leaked=0`

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
│  • SIGHUP 监听  │
│  • 共享内存管理 │
│  • Worker 监控  │
└────────┬────────┘
         │ fork()
    ┌────┴────┐
    │         │
┌───▼───┐ ┌──▼───┐  ← Worker 进程池
│Worker1│ │Worker2│  - epoll 事件循环
│gen=2  │ │gen=2  │  - HTTP 解析 + 代理
└───┬───┘ └──┬───┘  - 上游连接（异步）
    │         │      - 连接排空逻辑
    └────┬────┘
         │
    ┌────▼────┐
    │ 后端集群 │  ← 上游服务器（加权轮询）
    └─────────┘

平滑重启流程：
1. SIGHUP 触发
2. Master 增加 generation (0→1)
3. Master 设置 shutting_down=1
4. 旧 Worker 检测到 gen>my_gen，进入排空模式
5. Master fork 新 Worker（读取新 gen）
6. 旧 Worker 排空存量连接后退出
7. 新 Worker 接管新请求
```

### 核心模块

```
src/
├── main.c              # 入口，加载配置，启动 Master
├── master.c            # Master 进程（共享内存、generation 管理、Worker spawn）
├── worker.c            # Worker 进程，事件循环 + 状态机 + 连接排空
├── epoll_loop.c        # epoll 封装，事件分发
├── acceptor.c          # listen + accept，SO_REUSEPORT
├── connection.c        # 连接池 + 状态机定义
├── http_parser.c       # HTTP/1.1 状态机解析器
├── http_header.c       # HTTP 头部哈希表检索，支持头部增删
├── http_rewrite.c      # HTTP 头部改写（RFC 7239 标准代理头部）
├── upstream.c          # 加权轮询，后端节点管理
├── proxy.c             # 上游连接（非阻塞 socket）
├── config.c            # INI 配置解析，支持自定义头部配置
├── log.c               # Nginx 风格 access/error 日志（分离 Master/Worker 日志）
└── util.c              # 字符串/内存/时间工具

include/
├── nexus_master.h      # Master 进程接口（共享内存、generation 变量）
├── nexus_worker.h      # Worker 进程接口
└── ...                 # 其他模块头文件
```

### 异步 I/O 流程

```c
客户端连接 ──► READING_REQ ──► HTTP 头部改写 ──► CONNECTING_UPSTREAM ──► READING_UPSTREAM ──► WRITING_CLIENT
               (读请求)        (注入代理头部)       (非阻塞 connect)         (读响应)            (转发客户端)
               记录开始时间 → 注入 Req-ID → 生成访问日志
```

每个连接维护一个状态机，通过 epoll 事件驱动状态转换，Worker 线程永不阻塞。同时记录请求耗时，生成完整的访问日志。

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

[custom_headers]
# 自定义头部配置（可选）
# 格式：key:value，多个头部用逗号分隔
# headers = X-Gateway-Version:1.0.0,X-Cluster:beijing
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
# 基本测试
curl http://127.0.0.1:8080/

# 平滑重启测试
curl http://127.0.0.1:8080/slow &  # 慢速请求
sleep 0.5
kill -HUP $(pidof nexus_gateway | head -1)  # 触发热重启
wait  # 等待慢速请求完成

# 多轮热重启测试
for i in {1..3}; do
    curl http://127.0.0.1:8080/slow &
    sleep 0.5
    kill -HUP $(pidof nexus_gateway | head -1)
    wait
    sleep 2
done

# 检查日志
tail -20 logs/master.log
tail -30 logs/error.log
```

### 访问日志示例

```
[06/Jul/2026:22:20:39 +0800] 127.0.0.1:54321 - - [06/Jul/2026:22:20:39 +0800] "GET /api/test HTTP/1.1" 200 1234 "-" "curl/7.81.0" req_id=3k5tm9hq upstream=127.0.0.1:19998 duration=5.2ms
```

日志字段说明：
- `127.0.0.1:54321`：客户端 IP 和端口
- `[06/Jul/2026:22:20:39 +0800]`：请求时间戳
- `"GET /api/test HTTP/1.1"`：请求行
- `200`：HTTP 状态码
- `1234`：响应字节数
- `req_id=3k5tm9hq`：全局唯一请求 ID（Base36 编码）
- `upstream=127.0.0.1:19998`：上游服务器地址
- `duration=5.2ms`：请求总耗时（毫秒）

### HTTP 头部改写

网关会自动注入以下标准代理头部（符合 RFC 7239）：
- `X-Real-IP`：客户端真实 IP
- `X-Forwarded-For`：客户端真实 IP（清洗伪造值）
- `X-Forwarded-Host`：原始 Host 头部
- `X-Forwarded-Proto`：协议类型（http）
- `Via`：代理版本信息（`1.1 nexus-gateway/1.0.0`）
- `X-Gateway-Req-Id`：全局唯一请求追踪 ID

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

### ✅ 阶段 1.5：HTTP 头部改写与增强日志

- [x] Task 1.5.1: http_header 模块增强（头部增删接口）
- [x] Task 1.5.2: http_rewrite 模块（RFC 7239 标准代理头部）
- [x] Task 1.5.3: Req-ID 生成（Base36 全局唯一 ID）
- [x] Task 1.5.4: config 模块扩展（自定义头部配置）
- [x] Task 1.5.5: log 模块增强（Nginx 风格完整访问日志）
- [x] Task 1.5.6: acceptor 模块扩展（提取客户端地址）
- [x] Task 1.5.7: worker 集成（HTTP 改写 + 完整日志）

### ✅ 阶段 2：高可用

- [x] Task 2.1: master 进程 + 共享内存双缓冲
- [x] Task 2.2: SO_REUSEPORT 多进程监听 + fork Worker
- [x] Task 2.3: 平滑重启 + 存量连接排空

**高可用特性：**
- **Master-Worker 架构**：1 个 Master 管理 N 个 Worker（默认 CPU 核数）
- **共享内存配置**：双缓冲机制，支持配置热更新，零停机时间
- **Generation 机制**：用计数器区分重启轮次，避免新 Worker 误响应旧信号
- **平滑重启**：SIGHUP 触发，旧 Worker 排空存量连接后退出，新 Worker 接管
- **连接不丢失**：进行中的请求被旧 Worker 处理完毕，不会中断
- **零泄漏保证**：实测多轮热重启，`leaked=0`，所有 Worker 正常排空
- **自动 respawn**：Worker 崩溃后 Master 自动 fork 新 Worker

**测试验证：**
- ✅ 单轮热启动：5 个正常请求 + 1 个慢速请求 + 5 个新请求，全部成功
- ✅ 两轮热启动：连续两次 SIGHUP，三代 Worker 演进（gen 0→1→2），全部排空
- ✅ 泄漏测试：多轮重启均为 `leaked=0`，无 Worker 被强杀

### ✅ 阶段 3：功能增强

- [x] 限流模块（Token bucket，per-IP）
- [x] 黑名单模块（IP 访问控制）
- [x] 零拷贝模块（sendfile/splice 封装）
- [x] 健康检测模块（TCP 探测 + 后台线程）
- [x] 静态文件服务（Content-Type 检测 + sendfile）

**零拷贝说明：**
- 代码已实现 `nexus_zc_sendfile()` 和 `nexus_zc_splice()`
- 部分环境（如老内核或容器）可能不支持 `sendfile(socket → socket)`

### ✅ 阶段 4：集成 + 压测

- [x] 集成所有模块到 worker（路由分发 + 黑名单/限流检查）
- [x] wrk 压测脚本 + 报告（详见 [bench/result.md](bench/result.md)）
- [x] README + 文档完善

## 技术亮点

### 1. 异步 I/O 模型

- **非阻塞 socket**：所有 socket 设置 `SOCK_NONBLOCK`，connect/read/write 立即返回
- **状态机驱动**：每个连接维护状态（READING_REQ → CONNECTING_UPSTREAM → READING_UPSTREAM）
- **epoll 事件驱动**：只处理可读/可写事件，避免忙等待
- **连接排空**：Worker 检测到 `generation > my_generation` 时停止 accept，等待现有连接处理完毕后退出

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

### 5. HTTP 头部改写

- **RFC 7239 标准**：自动注入标准代理头部（X-Real-IP、X-Forwarded-*、Via）
- **安全防护**：清洗客户端伪造的代理头部，防止 IP 欺骗
- **请求追踪**：全局唯一的 Req-ID（Base36 编码），便于分布式追踪
- **自定义头部**：支持配置文件添加自定义业务头部

### 6. Nginx 风格日志

- **完整访问日志**：记录客户端地址、请求行、状态码、字节数、User-Agent、Req-ID、上游地址、耗时
- **时间戳格式**：与 Nginx 兼容的时间戳格式（`[06/Jul/2026:22:20:39 +0800]`）
- **高性能写入**：行缓冲 + 线程安全，支持多 Worker 并发写入

## 设计文档

- [完整设计文档](docs/superpowers/specs/2026-07-06-nexus-gateway-design.md)
- [实现计划](docs/superpowers/plans/2026-07-06-nexus-gateway.md)

## License

MIT License

## 作者

chenzhou0071

---

**本项目为学习项目，仅用于技术研究和教育目的。生产环境请使用 Nginx、HAProxy 等成熟方案。**
