# Nexus-Gateway 设计文档

> 日期：2026-07-06
> 范围：轻量级高可用网关（四层 TCP 负载均衡 + 七层 HTTP 代理），全量 9 模块

## 1. 项目目标与定位

从零实现一款 **Master-Worker 多进程高可用网关**，兼具四层 TCP 负载均衡与七层 HTTP 代理能力。

- **不依赖** Nginx / libcurl / 第三方网络框架，所有网络、协议解析、并发调度自主实现
- 对标 Tengine/SLB 核心设计思想
- 定位：本科生底层中间件级项目，作为"AI应用-存储内核-高并发网络"技术闭环的一环

## 2. 核心需求（9 大模块）

### 2.1 四层 TCP 负载均衡
- epoll 高并发连接管理
- 加权轮询流量分发
- 后端健康检测 + 故障自动剔除
- 多进程分流（Master-Worker）

### 2.2 七层 HTTP 代理
- 手写 HTTP/1.1 状态机解析器
- 路由匹配、静态文件、动态代理
- IP 限流、黑名单、非法请求过滤
- 哈希表头检索（O(1) 查询）

### 2.3 高级工程特性
- 零拷贝（sendfile / splice）
- 平滑重启 + 配置热加载
- 哈希优化 HTTP 头查询
- 健康检查 + 故障剔除

## 3. 关键决策（已确认）

| 维度 | 决定 | 理由 |
|---|---|---|
| 运行环境 | Windows 写代码，Linux 虚拟机跑（共享文件夹） | 用户实际工作流 |
| 范围 | 全量 9 模块 | 用户拍板 |
| 语言/构建 | 纯 C + Makefile | 最贴近系统调用，零依赖 |
| 进程模型 | SO_REUSEPORT 多进程同端口 | 避免惊群，Worker 独立重启 |
| 配置格式 | 自研 INI 解析器 | 几十行可控，无第三方依赖 |
| 配置热更 | 共享内存双缓冲 + atomic 切换 | lock-free，无脏读 |
| HTTP 范围 | MVP（GET/POST/HEAD/Content-Length/keep-alive） | 范围可控 |
| 压测/日志 | 文本日志 + wrk 压测脚本 | 出简历量化数据 |
| 实施顺序 | 先骨架后亮点 | 每步可运行，早出可演示版本 |

## 4. 整体架构

```
                config.ini
                    │ (inotify IN_MODIFY)
                    ▼
        ┌──────────────────────┐
        │  Master 进程 (1个)   │
        │  - 解析新配置         │
        │  - 写双缓冲非活跃槽   │
        │  - atomic 切换指针    │
        │  - 通知 Worker 检查   │
        │  - fork Worker       │
        │  - SIGHUP → 平滑重启  │
        └──────────┬───────────┘
                   │ fork() + mmap 共享内存
   ┌───────────────┼───────────────┐
   ▼               ▼               ▼
┌────────┐    ┌────────┐    ┌────────┐
│Worker 0│    │Worker 1│... │Worker N│   N = CPU 核数
│ epoll  │    │ epoll  │    │ epoll  │
│ listen │    │ listen │    │ listen │   ← SO_REUSEPORT 同端口
└───┬────┘    └────┬───┘    └────┬───┘
    │             │             │
    └─────────────┼─────────────┘
                  ▼
   ┌─────────────────────────────────────┐
   │ 共享内存: config_shadow[2]         │
   │   - 完整配置二进制快照               │
   │   - atomic active_slot (0/1)        │
   │   - lock-free, 永远读到完整版本      │
   └─────────────────────────────────────┘
```

**核心保证**：
- Master 不处理业务流量，只管配置与进程生命周期
- Worker 进程隔离，单 Worker 崩溃不影响整体服务
- SO_REUSEPORT 让内核做连接负载均衡，避免 accept 惊群
- 配置热更走共享内存双缓冲，lock-free，Worker 读到永远是完整配置

## 5. 模块拆分

按职责单一原则拆成 7 层、16 个 .c 文件：

| 层 | 模块 | 职责 |
|---|---|---|
| 进程 | `master.c` | 主循环、信号、fork、共享内存映射 |
| 进程 | `worker.c` | 事件循环、连接调度 |
| IO | `epoll_loop.c` | epoll 封装、事件分发 |
| IO | `acceptor.c` | listen socket + SO_REUSEPORT |
| IO | `connection.c` | 连接池（fd → connection 映射） |
| 协议 | `http_parser.c` | HTTP/1.1 状态机 |
| 协议 | `http_header.c` | 哈希表头检索 |
| 协议 | `upstream.c` | 后端节点 + 加权轮询 + 健康检查 |
| 转发 | `proxy.c` | 反向代理、零拷贝 |
| 转发 | `static_file.c` | 静态文件服务（sendfile） |
| 横切 | `config.c` | INI 解析 + 双缓冲 + atomic 切换 |
| 横切 | `rate_limit.c` | IP 限流（令牌桶） |
| 横切 | `blacklist.c` | 黑名单（哈希集合） |
| 横切 | `log.c` | access.log / error.log |
| 横切 | `util.c` | 字符串、内存、时间、原子工具 |

每个模块**对外只暴露一个头文件**（`include/nexus_*.h`），内部状态全私有。

## 6. 一次 HTTP 请求的数据流

```
客户端                Worker                  上游/磁盘
  │                    │                          │
  │── TCP SYN ───────▶│                          │
  │                    │ epoll EPOLLIN on listen  │
  │                    │ acceptor.c: accept()     │
  │                    │ connection.c: 创建 conn  │
  │◀ SYN-ACK ─────────│                          │
  │                    │                          │
  │── GET /api ──────▶│                          │
  │                    │ http_parser 状态机       │
  │                    │   REQ_LINE → HEADERS     │
  │                    │   → BODY → DONE          │
  │                    │ http_header 入哈希表     │
  │                    │                          │
  │                    │ rate_limit 检查 IP       │
  │                    │ blacklist 检查 IP        │
  │                    │                          │
  │                    │ config 读共享内存查路由  │
  │                    │ upstream 加权轮询选后端  │
  │                    │                          │
  │                    │ proxy 连接上游 ─────────▶│
  │                    │       转发 HTTP 请求 ───▶│
  │                    │                          │
  │                    │ proxy 收上游响应 ◀──────│
  │                    │   (splice/sendfile)      │
  │◀ HTTP/1.1 200 ────│                          │
  │                    │ log 写 access.log        │
  │                    │ connection 释放/keepalive│
```

## 7. connection 状态机

每个 TCP 连接对应一个 `connection` 对象，状态机驱动其生命周期：

```
CONN_IDLE
   │ (accept)
   ▼
CONN_READING_REQ ◀────── keep-alive 回到这里
   │ (解析完成)
   ▼
CONN_CONNECTING_UPSTREAM
   │ (上游 connect 成功)
   ▼
CONN_WRITING_UPSTREAM
   │ (请求发完)
   ▼
CONN_READING_UPSTREAM
   │ (响应头收完)
   ▼
CONN_WRITING_CLIENT
   │ (响应发完)
   ▼
CONN_CLOSING ──▶ CONN_FREE（回连接池）
```

## 8. 零拷贝策略

| 场景 | API | 触发条件 |
|---|---|---|
| 静态文件响应 | `sendfile(client_fd, file_fd, offset, count)` | `Content-Type` 已知且无动态处理 |
| 代理大包转发 | `splice(client_fd, NULL, pipe[1], NULL, 65536, 0)` + `splice(pipe[0], NULL, upstream_fd, ...)` | 响应体 > 16KB |
| 小包响应 | 普通 `write()` | < 16KB（小包零拷贝反而慢） |

## 9. 配置热更机制（双缓冲 + atomic 切换）

```c
struct config_shadow {
    uint64_t version;           // 单调递增
    uint32_t total_size;        // data[] 有效字节数
    char     data[CONFIG_MAX];  // 完整配置的二进制快照
};

// 全局：双缓冲槽 + 当前活跃槽
struct config_shadow g_shadow[2];
atomic_int g_active_slot;  // 0 或 1

// Master 侧：reload
void reload_config() {
    int inactive = 1 - atomic_load(&g_active_slot);
    parse_to_buffer(g_shadow[inactive].data, CONFIG_MAX);
    g_shadow[inactive].total_size = ...;
    atomic_fetch_add(&g_shadow[inactive].version, 1);
    __sync_synchronize();  // 内存屏障
    atomic_store(&g_active_slot, inactive);
}

// Worker 侧：事件循环开头
void worker_tick() {
    int slot = atomic_load(&g_active_slot);
    if (slot != last_seen_slot) {
        apply_config(&g_shadow[slot]);
        last_seen_slot = slot;
    }
}
```

**保证**：Worker 永远读到完整旧配置或完整新配置，**绝不读半截**。lock-free，无锁争用。

## 10. 平滑重启流程（SIGHUP）

1. Master 收到 SIGHUP
2. Master 重新读 config.ini，解析
3. Master fork 一批新 Worker，新 Worker 初始化 epoll + listen
4. 新 Worker 通过 pipe 通知 Master "我准备好了"
5. Master 通过共享内存设置 `shutting_down=1` 标志
6. 旧 Worker 在事件循环检测到 `shutting_down=1` → 停止 accept 新连接
7. 旧 Worker 继续处理存量连接直到全部关闭
8. 旧 Worker 自然退出
9. 全程**无断流**

## 11. 错误处理

| 场景 | 处理 |
|---|---|
| 畸形 HTTP 请求 | 状态机进 INVALID → 立即关连接 → error.log |
| 后端超时 | upstream 标 unhealthy → 加权轮询跳过 → 健康检查继续 |
| 后端全挂 | 502 Bad Gateway（自定义错误页） |
| Worker 崩溃 | Master 通过 SIGCHLD 检测 → 立即 fork 新 Worker |
| Master 崩溃 | 整个网关挂（**已知缺陷，单 Master 模式**） |
| 配置错误 | Master 校验失败 → 保留旧配置 + error.log |
| 磁盘满 | 日志降级：丢弃新日志，error.log 告警一次 |
| fd 耗尽 | accept 后立即关闭，log 告警 |
| CPU 100% 攻击 | max_connections + per-IP 限流兜底 |

## 12. 测试策略（四层递进）

### 12.1 单元测试
- `tests/test_http_parser.c` — 真实请求回放
- `tests/test_config.c` — INI 边界
- `tests/test_hash_header.c` — 正确性 + 性能
- `tests/test_weighted_roundrobin.c` — 权重分配均匀

### 12.2 集成测试
- `tests/integration/test_proxy.sh` — 上游 + 网关 + curl 验证
- `tests/integration/test_static_file.sh` — sendfile 验证
- `tests/integration/test_rate_limit.sh` — 限流命中
- `tests/integration/test_hot_reload.sh` — 改配置后旧连接不中断
- `tests/integration/test_smooth_restart.sh` — SIGHUP 时传输中请求不丢

### 12.3 压测
- `bench/bench.sh` — `wrk -t8 -c1000 -d30s`
- 对比：单进程 vs 多进程 / 有零拷贝 vs 无零拷贝
- 输出 `bench/result.md`（简历"QPS 提升 25%"数据来源）

### 12.4 混沌测试
- 随机 kill -9 Worker，验证 Master 拉起
- 上游中途挂掉，验证故障剔除 + 自动恢复
- 配置改非法格式，验证旧配置继续生效

## 13. 文件清单

```
nexus_gateway/
├── Makefile
├── config.ini.example
├── README.md
├── src/
│   ├── master.c / worker.c
│   ├── epoll_loop.c / acceptor.c / connection.c
│   ├── http_parser.c / http_header.c / upstream.c
│   ├── proxy.c / static_file.c
│   ├── config.c / rate_limit.c / blacklist.c
│   └── log.c / util.c
├── include/
│   └── nexus_*.h            (每个 .c 对应一个 .h)
├── tests/
│   ├── test_*.c             (单元测试)
│   └── integration/test_*.sh
├── bench/
│   ├── bench.sh
│   └── result.md
└── docs/
    ├── plan/...             (原始设计文档)
    └── superpowers/specs/   (本设计文档)
```

## 14. 实施阶段（先骨架后亮点）

### 阶段 1：骨架（MVP 端到端）
epoll + 单 Worker + HTTP 解析 + 基本代理 + 加权轮询
**可演示**：能转发 HTTP 请求到上游

### 阶段 2：高可用
Master-Worker + 平滑重启 + 热重载（双缓冲）
**可演示**：SIGHUP 不掉线，改配置不重启

### 阶段 3：亮点
零拷贝 + 哈希表 + 限流 + 黑名单 + 健康检查
**可演示**：完整 9 模块齐备

### 阶段 4：压测出报告
wrk 跑出数据，写入 README 和 bench/result.md
**可演示**：性能对比数据

## 15. 简历三行总结（最终目标）

> 自研 Master-Worker 多进程高可用网关，实现四层 TCP 负载均衡与七层 HTTP 流量治理，对标 Nginx/Tengine 核心架构。
>
> 基于 epoll 实现高并发网络模型，引入 sendfile/splice 零拷贝机制优化文件与大包传输，压测 QPS 提升 25%；手写 HTTP 状态机解析器并通过哈希表优化头部检索效率。
>
> 实现配置热加载与平滑重启能力，完成后端节点健康检测、IP 限流、黑名单防护，保障服务高可用，具备工业级中间件设计思维。

## 16. 已知缺陷 / 后续改进

- **单 Master 模式**：Master 挂整个网关挂。改进方向是双 Master + VRRP（但已是另一个项目级工作量）
- **不支持 HTTPS**：需要 OpenSSL，依赖增加。改进方向是 ALPN 终结（后续）
- **不支持 HTTP/2**：单连接多路复用，协议差异大
- **无指标暴露**：Prometheus exporter 可作为 v2 增量
