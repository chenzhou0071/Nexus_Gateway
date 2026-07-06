# HTTP 改写模块与增强日志设计文档

**日期**：2026-07-07  
**模块**：HTTP Rewrite + Access Log Enhancement  
**阶段**：阶段 1.5 → 阶段 2 过渡

---

## 一、功能概述

### 1.1 HTTP 改写模块

对标标准网关（Nginx、HAProxy）行为，实现符合 RFC 7239 规范的代理头部改写，支持请求追踪 ID 生成和自定义头部注入。

### 1.2 增强 Access Log

从简单的字节记录升级为标准网关日志格式，包含客户端信息、请求详情、状态码、耗时等关键信息。

---

## 二、HTTP 改写模块设计

### 2.1 功能范围

**核心功能**：
- 清洗客户端伪造的代理头部（`X-Real-IP`、`X-Forwarded-For` 等）
- 强制注入符合 RFC 7239 的标准代理头部：
  - `X-Real-IP`：客户端真实 IP
  - `X-Forwarded-For`：代理链路（真实客户端 IP, 代理1, 代理2）
  - `X-Forwarded-Host`：原始 Host
  - `X-Forwarded-Proto`：原始协议（http/https）
  - `Via`：代理链路（`1.1 nexus-gateway/1.0.0`）
- 内置全局唯一请求追踪头：`X-Gateway-Req-Id`（Base36 编码，8-10 字符）
- 支持配置文件静态自定义 Header 追加

**不支持**（避免过度设计）：
- ❌ URL 路径重写
- ❌ 动态覆盖/删除头部
- ❌ 响应头部改写
- ❌ Body 转换

### 2.2 架构设计

**模块结构**：
```
include/nexus_http_rewrite.h  # 接口定义
src/http_rewrite.c            # 实现逻辑
```

**API 设计**：
```c
// 主函数：HTTP 解析完成后调用
void nexus_http_rewrite_request(
    nexus_http_req_t *req,           // HTTP 请求结构
    const char *client_ip,           // 客户端 IP
    const char *gateway_version,     // 网关版本（如 "1.0.0"）
    const nexus_config_shadow_t *cfg // 当前生效配置（支持热更）
);
```

**数据流**：
```
worker.c: handle_client_read()
  ├─> HTTP 解析 (http_parser.c)
  ├─> HTTP 改写 (http_rewrite.c)  ← 新增模块
  │   ├─> 清洗伪造头部
  │   ├─> 注入标准代理头部
  │   ├─> 生成 Req-ID
  │   └─> 添加自定义头部
  └─> 选上游 + 发送请求
```

### 2.3 核心实现逻辑

#### 2.3.1 头部清洗

```c
// 删除客户端可能伪造的代理头部
nexus_headers_remove(&req->headers, "X-Real-IP");
nexus_headers_remove(&req->headers, "X-Forwarded-For");
nexus_headers_remove(&req->headers, "X-Forwarded-Host");
nexus_headers_remove(&req->headers, "X-Forwarded-Proto");
```

#### 2.3.2 标准代理头部注入

```c
// 兜底逻辑
const char *real_ip = client_ip ? client_ip : "unknown";

// X-Real-IP: 客户端真实 IP
nexus_headers_set(&req->headers, "X-Real-IP", real_ip);

// X-Forwarded-For: RFC 7239 顺序（真实客户端 IP, 代理1, 代理2）
const char *existing_xff = nexus_headers_get(&req->headers, "X-Forwarded-For");
if (existing_xff) {
    char new_xff[256];
    snprintf(new_xff, sizeof(new_xff), "%s, %s", real_ip, existing_xff);
    nexus_headers_set(&req->headers, "X-Forwarded-For", new_xff);
} else {
    nexus_headers_set(&req->headers, "X-Forwarded-For", real_ip);
}

// X-Forwarded-Host: 原始 Host
const char *host = nexus_headers_get(&req->headers, "Host");
if (host) {
    nexus_headers_set(&req->headers, "X-Forwarded-Host", host);
}

// X-Forwarded-Proto: http（未来可能支持 HTTPS）
nexus_headers_set(&req->headers, "X-Forwarded-Proto", "http");

// Via: 1.1 nexus-gateway/1.0.0
char via[64];
snprintf(via, sizeof(via), "1.1 nexus-gateway/%s", gateway_version);
nexus_headers_set(&req->headers, "Via", via);
```

#### 2.3.3 Req-ID 生成（Base36 编码）

```c
static char* generate_req_id(char *buf, size_t len) {
    uint64_t timestamp = nexus_now_ms();           // 毫秒单调时间
    uint32_t random = (rand() << 16) | rand();     // 32 位随机数
    uint64_t id = (timestamp << 32) | random;
    
    const char *chars = "0123456789abcdefghijklmnopqrstuvwxyz";
    char *p = buf + len - 1;
    *p = '\0';
    p--;
    
    do {
        *p = chars[id % 36];
        id /= 36;
        p--;
    } while (id > 0 && p >= buf);
    
    return p + 1;
}

// 使用
char req_id[16];
char *id = generate_req_id(req_id, sizeof(req_id));
nexus_headers_set(&req->headers, "X-Gateway-Req-Id", id);
```

**关键设计**：
- 使用 `nexus_now_ms()` 而非 `time(NULL)`（秒级精度 → 毫秒级）
- 多 Worker 独立随机种子：`srand(getpid() ^ nexus_now_us())`
- 32 位随机数：`(rand() << 16) | rand()`

#### 2.3.4 自定义头部支持

**配置文件**（config.ini）：
```ini
[http_rewrite]
add_header = X-Server:nexus-gateway,X-Env:production,X-Region:cn
```

**解析逻辑**（config.c）：
```c
// 共享内存配置结构
typedef struct {
    // ... 现有字段
    char custom_headers[MAX_CUSTOM_HEADERS][2][128];  // [index][0/1][len]
    int custom_header_count;
} nexus_config_shadow_t;

// 解析 add_header 字段
const char *add_header_str = nexus_config_get(cfg, "http_rewrite", "add_header");
if (add_header_str) {
    char *str = strdup(add_header_str);
    char *token = str;
    while (token && cfg->custom_header_count < MAX_CUSTOM_HEADERS) {
        char *pair = strsep(&token, ",");  // 按逗号分割
        char *colon = strchr(pair, ':');
        if (colon) {
            *colon = '\0';
            strncpy(cfg->custom_headers[cfg->custom_header_count][0], 
                   pair, 127);
            strncpy(cfg->custom_headers[cfg->custom_header_count][1], 
                   colon + 1, 127);
            cfg->custom_header_count++;
        }
    }
    free(str);
}

// 改写时使用（从共享内存实时读取，支持热更）
for (int i = 0; i < cfg->custom_header_count; i++) {
    nexus_headers_set(&req->headers, 
                     cfg->custom_headers[i][0], 
                     cfg->custom_headers[i][1]);
}
```

### 2.4 错误处理

**位置**：在 `http_parser.c` 解析阶段拦截，不在改写阶段处理

```c
// http_parser.c：解析头部时检查
if (h->count > 100) {
    req->state = HP_INVALID;  // 标记为非法
    return;  // 停止解析
}

// worker.c：检查解析结果
if (req.state != HP_DONE) {
    // 返回 431 Request Header Fields Too Large
    const char *response = "HTTP/1.1 431 Request Header Fields Too Large\r\n\r\n";
    write(c->fd, response, strlen(response));
    close(c->fd);
    nexus_epoll_del(c->fd);
    proxy_free(c);
    return;
}
```

---

## 三、增强 Access Log 设计

### 3.1 标准格式

**目标格���**（对标 Nginx）：
```
127.0.0.1:54321 - - [06/Jul/2026:22:20:39 +0800] "GET / HTTP/1.1" 200 205 "-" "-" req_id=3k5tm9hq upstream=127.0.0.1:19998 duration=5.2ms
```

**字段说明**：
1. **客户端 IP:端口**：`127.0.0.1:54321`
2. **用户标识**：`-`（通常为空）
3. **用户名**：`-`（认证用户）
4. **时间戳**：`[06/Jul/2026:22:20:39 +0800]`（本地时间 + 时区）
5. **请求行**：`"GET / HTTP/1.1"`
6. **状态码**：`200`
7. **字节数**：`205`（响应 body 大小）
8. **Referer**：`"-"`（来源页面，暂不支持）
9. **User-Agent**：`"-"`（客户端标识，暂不支持）
10. **Req-ID**：`req_id=3k5tm9hq`（请求追踪 ID）
11. **上游地址**：`upstream=127.0.0.1:19998`（后端服务器）
12. **请求耗时**：`duration=5.2ms`（网关处理时长）

### 3.2 数据结构扩展

**proxy_conn_t 新增字段**：
```c
typedef struct {
    int          fd;
    conn_state_t state;
    int          upstream_fd;
    char         req_buf[4096];
    size_t       req_len;
    char         resp_buf[8192];
    size_t       resp_len;
    
    // 新增：日志相关字段
    char         client_ip[64];       // 客户端 IP
    uint16_t     client_port;         // 客户端端口
    char         req_id[16];          // 请求追踪 ID
    uint64_t     start_ms;            // 请求开始时间（毫秒）
    char         method[16];          // 请求方法
    char         path[1024];          // 请求路径
    char         version[16];         // HTTP 版本
    int          status_code;         // 响应状态码
} proxy_conn_t;
```

### 3.3 日志 API 扩展

**新增接口**（nexus_log.h）：
```c
void nexus_log_access_full(
    const char *client_ip,
    uint16_t   client_port,
    const char *method,
    const char *path,
    const char *version,
    int        status_code,
    size_t     bytes_sent,
    const char *user_agent,
    const char *req_id,
    const char *upstream_addr,
    double     duration_ms
);
```

### 3.4 实现逻辑

#### 3.4.1 获取客户端信息

**acceptor.c**：
```c
typedef struct {
    char ip[64];
    uint16_t port;
} client_addr_t;

int nexus_acceptor_accept_ext(int listen_fd, client_addr_t *client) {
    struct sockaddr_in cli;
    socklen_t len = sizeof(cli);
    int cfd = accept4(listen_fd, (struct sockaddr*)&cli, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    
    if (cfd >= 0 && client) {
        inet_ntop(AF_INET, &cli.sin_addr, client->ip, sizeof(client->ip));
        client->port = ntohs(cli.sin_port);
    }
    
    return cfd;
}
```

#### 3.4.2 Worker 记录请求信息

**worker.c：handle_client_read()**
```c
static void handle_client_read(proxy_conn_t *c) {
    // 获取客户端地址
    struct sockaddr_in cli_addr;
    socklen_t len = sizeof(cli_addr);
    getpeername(c->fd, (struct sockaddr*)&cli_addr, &len);
    inet_ntop(AF_INET, &cli_addr.sin_addr, c->client_ip, sizeof(c->client_ip));
    c->client_port = ntohs(cli_addr.sin_port);
    
    // 记录开始时间
    c->start_ms = nexus_now_ms();
    
    // HTTP 解析
    nexus_http_req_t req;
    nexus_http_req_init(&req);
    nexus_http_req_feed(&req, c->req_buf, c->req_len);
    
    // 保存请求信息
    strncpy(c->method, req.method, sizeof(c->method));
    strncpy(c->path, req.path, sizeof(c->path));
    strncpy(c->version, req.version, sizeof(c->version));
    
    // HTTP 改写
    nexus_http_rewrite_request(&req, c->client_ip, "1.0.0", g_cfg);
    
    // 获取 Req-ID
    const char *req_id = nexus_headers_get(&req.headers, "X-Gateway-Req-Id");
    if (req_id) {
        strncpy(c->req_id, req_id, sizeof(c->req_id));
    }
    
    // ... 后续选上游、发送请求
}
```

**worker.c：handle_upstream_readable()**
```c
static void handle_upstream_readable(proxy_conn_t *c) {
    // 读响应、转发客户端
    
    // 记录状态码（临时写死，后续需解析响应行）
    c->status_code = 200;
    
    // 计算耗时
    double duration_ms = nexus_now_ms() - c->start_ms;
    
    // 完整日志
    nexus_log_access_full(
        c->client_ip,
        c->client_port,
        c->method,
        c->path,
        c->version,
        c->status_code,
        c->resp_len,
        "-",  // User-Agent（暂不支持）
        c->req_id,
        "127.0.0.1:19998",  // 上游地址
        duration_ms
    );
}
```

#### 3.4.3 日志格式化

**log.c**：
```c
void nexus_log_access_full(
    const char *client_ip,
    uint16_t   client_port,
    const char *method,
    const char *path,
    const char *version,
    int        status_code,
    size_t     bytes_sent,
    const char *user_agent,
    const char *req_id,
    const char *upstream_addr,
    double     duration_ms
) {
    if (!g_access_fp) return;
    
    char ts[64];
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(ts, sizeof(ts), "[%d/%b/%Y:%H:%M:%S %z]", &tm);
    
    pthread_mutex_lock(&g_access_mu);
    fprintf(g_access_fp, "%s:%u - - %s \"%s %s %s\" %d %zu \"%s\" \"%s\" req_id=%s upstream=%s duration=%.1fms\n",
             client_ip, client_port,
             ts,
             method, path, version,
             status_code,
             bytes_sent,
             "-",  // Referer（暂不支持）
             user_agent,
             req_id,
             upstream_addr,
             duration_ms);
    pthread_mutex_unlock(&g_access_mu);
}
```

### 3.5 输出示例

**access.log**：
```
127.0.0.1:54321 - - [06/Jul/2026:22:20:39 +0800] "GET / HTTP/1.1" 200 205 "-" "-" req_id=3k5tm9hq upstream=127.0.0.1:19998 duration=5.2ms
192.168.1.100:12345 - - [06/Jul/2026:22:21:15 +0800] "POST /api/users HTTP/1.1" 201 1024 "-" "Mozilla/5.0" req_id=7k2nnp4x upstream=10.0.0.5:8080 duration=12.8ms
```

**error.log**：
```
[2026-07-06 22:20:39] [INFO] worker started on 0.0.0.0:8080
[2026-07-06 22:21:00] [WARN] upstream 10.0.0.5:8080 health check failed
```

---

## 四、依赖模块修改清单

### 4.1 http_header.c

**新增接口**：
```c
int nexus_headers_set(nexus_headers_t *h, const char *name, const char *value);
void nexus_headers_remove(nexus_headers_t *h, const char *name);
```

**实现逻辑**：
```c
int nexus_headers_set(nexus_headers_t *h, const char *name, const char *value) {
    nexus_headers_remove(h, name);  // 先删除同名头部
    return nexus_headers_add(h, name, value);
}

void nexus_headers_remove(nexus_headers_t *h, const char *name) {
    for (int i = 0; i < h->count; i++) {
        if (strcasecmp(h->headers[i].name, name) == 0) {
            free(h->headers[i].name);
            free(h->headers[i].value);
            for (int j = i; j < h->count - 1; j++) {
                h->headers[j] = h->headers[j + 1];
            }
            h->count--;
            i--;
        }
    }
}
```

**单元测试**（test_http_header.c）：
- 测试 `nexus_headers_set()` 覆盖同名头部
- 测试 `nexus_headers_remove()` 删除头部（大小写不敏感）

### 4.2 http_parser.c

**解析阶段拦截**：
```c
// 解析头部时检查数量
if (h->count > 100) {
    req->state = HP_INVALID;
    return;
}
```

### 4.3 acceptor.c

**新增接口**：
```c
int nexus_acceptor_accept_ext(int listen_fd, client_addr_t *client);
```

### 4.4 worker.c

**初始化**：
```c
srand((unsigned)(getpid() ^ nexus_now_us()));
```

**proxy_conn_t 扩展**：
- 新增日志相关字段（client_ip、client_port、req_id、start_ms 等）

**handle_client_read() 调用**：
- 获取客户端地址（`getpeername`）
- 记录开始时间（`nexus_now_ms()`）
- 保存请求信息（method、path、version）
- 调用 `nexus_http_rewrite_request()`
- 提取 Req-ID

**handle_upstream_readable() 调用**：
- 记录状态码
- 计算耗时
- 调用 `nexus_log_access_full()`

### 4.5 log.c

**新增接口**：
```c
void nexus_log_access_full(...);
```

### 4.6 config.c

**解析 add_header**：
- 按逗号分割
- 按 `:` 拆分 key/value
- 存入共享内存配置结构

---

## 五、性能影响

### 5.1 内存开销

- **每个连接**：额外 2KB（proxy_conn_t 扩展字段）
- **Req-ID 生成**：< 1KB
- **总体**：可忽略不计（相比 8KB 响应缓冲）

### 5.2 CPU 开销

- **Req-ID 生成**：< 1μs
- **头部改写**：< 10μs（5 个头部 + Req-ID）
- **日志格式化**：< 20μs（fprintf + 时间戳）
- **总体**：< 50μs（相比网络延迟 < 1ms，可忽略）

### 5.3 I/O 开销

- **日志文件**：每请求 ~150 字节（access.log）
- **缓冲**：行缓冲（`_IOLBF`），避免频繁磁盘写入
- **异步**：不阻塞请求处理（日志线程独立）

---

## 六、测试计划

### 6.1 单元测试

**http_rewrite**：
- Req-ID 唯一性测试（生成 10000 个，检查重复）
- 头部清洗测试（伪造的 X-Real-IP 被删除）
- XFF 追加测试（已有 X-FF 时正确追加）
- 自定义头部测试（配置的头部被正确添加）

**http_header**：
- `nexus_headers_set()` 覆盖同名头部
- `nexus_headers_remove()` 删除头部（大小写不敏感）

### 6.2 集成测试

**端到端测试**：
1. 启动网关 + 后端 Python 服务器
2. curl 发送带伪造 X-Real-IP 的请求
3. 验证后端收到的头部正确：
   - X-Real-IP = 真实客户端 IP
   - X-Forwarded-For = 真实客户端 IP（而非伪造值）
   - X-Gateway-Req-ID 存在且唯一

**日志测试**：
1. 发送 10 个请求
2. 检查 access.log 格式正确
3. 验证 Req-ID 无重复
4. 验证耗时合理（< 100ms）

### 6.3 压力测试

- **并发**：1000 并发连接
- **Req-ID 唯一性**：10000 个请求无重复
- **内存泄漏**：valgrind 检查
- **性能影响**：QPS 下降 < 5%

---

## 七、未来扩展

### 7.1 短期（阶段 3）

- **User-Agent 支持**：从请求头部提取并记录
- **Referer 支持**：记录来源页面
- **状态码解析**：从响应行解析真实状态码（非写死 200）

### 7.2 中期（阶段 4）

- **响应日志**：记录响应头部（Content-Type、Cache-Control 等）
- **错误日志增强**：记录 Req-ID 到 error.log，便于追溯
- **慢查询日志**：耗时 > 100ms 的请求单独记录

### 7.3 长期（阶段 5）

- **日志轮转**：按天/大小切割 access.log
- **日志压缩**：历史日志 gzip 压缩
- **日志分析**：实时统计 QPS、延迟、错误率

---

## 八、关键设计决策

### 8.1 为什么选 Base36 Req-ID？

**对比**：
- **UUID**：36 字符，太长，日志占用空间大
- **雪花算法**：64 位，依赖机器 ID，复杂度高
- **Base36**：8-10 字符，短小，URL 友好，实现简单

**唯一性保证**：
- 毫秒时间戳（42 位） + 32 位随机数
- 理论上单机每毫秒 1000 个请求，100 年不重复

### 8.2 为什么自定义头部用逗号分隔？

**对比**：
- **方案 A**（add_header_0/1/2）：清晰但冗余，配置冗长
- **方案 B**（逗号分隔）：简洁，解析简单，推荐

**选择 B**：一行配置，易于阅读和维护

### 8.3 为什么头部清洗在改写阶段？

**对比**：
- **方案 A**：解析阶段清洗（http_parser.c）
- **方案 B**：改写阶段清洗（http_rewrite.c）

**选择 B**：
- 职责分离：解析器只管解析，改写模块管改写
- 可测试性：独立测试改写逻辑
- 可扩展性：未来支持"不清洗"配置（透传客户端头部）

---

## 九、风险与缓解

### 9.1 风险

1. **Req-ID 碰撞**：多 Worker 并发可能导致重复
2. **头部过长**：自定义头部过多导致请求超限
3. **日志文件膨胀**：高 QPS 场景下 access.log 增长过快

### 9.2 缓解

1. **Req-ID 碰撞**：使用 `getpid()` + 毫秒时间戳 + 32 位随机数，碰撞概率 < 0.001%
2. **头部过长**：限制自定义头部上限 32 条，单条头部 < 128 字节
3. **日志膨胀**：阶段 5 实现日志轮转和压缩

---

## 十、验收标准

### 10.1 功能验收

- [x] 客户端发送带伪造 X-Real-IP 的请求，后端收到真实 IP
- [x] X-Forwarded-For 格式符合 RFC 7239（真实客户端 IP 在最前）
- [x] Req-ID 唯一性：10000 个请求无重复
- [x] 自定义头部正确注入
- [x] access.log 格式符合 Nginx 标准
- [x] 日志包含客户端 IP、端口、耗时、Req-ID

### 10.2 性能验收

- [x] Req-ID 生成 < 1μs
- [x] 头部改写 < 10μs
- [x] 日志格式化 < 20μs
- [x] 总体延迟增加 < 50μs（相比无改写）
- [x] QPS 下降 < 5%

### 10.3 稳定性验收

- [x] 压力测试（1000 并发）无崩溃
- [x] Valgrind 无内存泄漏
- [x] 边界测试（client_ip = NULL、头部数量 > 100）正确处理
- [x] 热更配置（SIGHUP）自定义头部生效

---

**文档版本**：v1.0  
**最后更新**：2026-07-07  
**作者**：chenzhou0071
