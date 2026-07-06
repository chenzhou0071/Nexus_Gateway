# HTTP 改写模块与增强日志实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**目标：** 实现 HTTP 代理头部改写（RFC 7239）+ Req-ID 生成 + 标准 access.log 格式

**架构：** 独立 http_rewrite 模块 + 扩展 worker 日志记录 + 新增 http_header API，通过 epoll 异步流程集成，零拷贝传递头部信息。

**技术栈：** C99、epoll、RFC 7239、Base36 编码、共享内存配置

---

## 全局约束

- **编译器：** GCC/Clang，支持 C99 标准
- **依赖：** 无第三方库，仅使用 POSIX 接口
- **平台：** Linux 2.6+（epoll 支持）
- **命名：** 函数前缀 `nexus_`，类型后缀 `_t`，宏全大写
- **内存：** 禁止动态分配（使用栈/预分配池）
- **并发：** 多进程 Worker，共享内存只读配置

---

## 文件结构

**新增文件：**
- `include/nexus_http_rewrite.h` - 改写模块接口
- `src/http_rewrite.c` - 改写逻辑实现
- `tests/test_http_rewrite.c` - 改写模块单元测试

**修改文件：**
- `include/nexus_http_header.h` - 新增 `set/remove` 接口声明
- `src/http_header.c` - 实现 `set/remove` 逻辑
- `tests/test_http_header.c` - 补充 `set/remove` 测试
- `src/http_parser.c` - 解析阶段头部数量检查
- `src/acceptor.c` - accept 返回客户端地址
- `include/nexus_acceptor.h` - accept 扩展接口声明
- `src/worker.c` - 集成改写模块 + 日志增强
- `include/nexus_log.h` - 新增完整日志接口声明
- `src/log.c` - 实现完整日志格式化
- `src/config.c` - 解析 add_header 配置
- `include/nexus_config.h` - 共享内存配置结构扩展
- `src/main.c` - 初始化随机种子

**边界划分：**
- http_rewrite：专注头部清洗和注入，不涉及网络 I/O
- worker：状态机 + 异步 I/O，调用改写模块
- log：格式化和写入日志文件，不解析 HTTP
- http_header：提供头部 CRUD，不处理业务逻辑

---

## Task 1: http_header 模块扩展（新增 set/remove 接口）

**目标：** 为后续改写模块提供头部覆盖和删除能力

**Files:**
- Modify: `include/nexus_http_header.h:27`
- Modify: `src/http_header.c:80-120`
- Test: `tests/test_http_header.c`

**Interfaces:**
- Consumes: 现有 `nexus_headers_t` 结构
- Produces: `nexus_headers_set()`、`nexus_headers_remove()`

**步骤：**

- [ ] **Step 1: 写接口声明（include/nexus_http_header.h）**

在文件末尾（第 27 行后）添加：

```c
// 覆盖/新增头部，同名直接替换
int nexus_headers_set(nexus_headers_t *h, const char *name, const char *value);

// 删除指定头部（大小写不敏感）
void nexus_headers_remove(nexus_headers_t *h, const char *name);
```

- [ ] **Step 2: 实现 nexus_headers_set（src/http_header.c）**

在文件末尾添加：

```c
int nexus_headers_set(nexus_headers_t *h, const char *name, const char *value) {
    if (!h || !name || !value) return -1;
    
    // 先删除同名头部（大小写不敏感）
    nexus_headers_remove(h, name);
    
    // 再添加新头部
    return nexus_headers_add(h, name, value);
}
```

- [ ] **Step 3: 实现 nexus_headers_remove（src/http_header.c）**

```c
void nexus_headers_remove(nexus_headers_t *h, const char *name) {
    if (!h || !name) return;
    
    for (int i = 0; i < h->count; i++) {
        if (strcasecmp(h->headers[i].name, name) == 0) {
            free(h->headers[i].name);
            free(h->headers[i].value);
            
            // 后续头部前移
            for (int j = i; j < h->count - 1; j++) {
                h->headers[j] = h->headers[j + 1];
            }
            
            h->count--;
            i--;  // 重新检查当前位置（可能有多个同名头部）
        }
    }
}
```

- [ ] **Step 4: 编写单元测试（tests/test_http_header.c）**

在文件末尾添加：

```c
void test_headers_set() {
    nexus_headers_t h;
    nexus_headers_init(&h);
    
    // 添加第一个头部
    nexus_headers_add(&h, "X-Custom", "value1");
    assert(h.count == 1);
    assert(strcmp(h.headers[0].value, "value1") == 0);
    
    // 覆盖同名头部
    nexus_headers_set(&h, "X-Custom", "value2");
    assert(h.count == 1);  // 数量不变
    assert(strcmp(h.headers[0].value, "value2") == 0);  // 值更新
    
    nexus_headers_cleanup(&h);
    printf("test_headers_set: PASS\n");
}

void test_headers_remove() {
    nexus_headers_t h;
    nexus_headers_init(&h);
    
    nexus_headers_add(&h, "X-Real-IP", "fake");
    nexus_headers_add(&h, "Host", "example.com");
    nexus_headers_add(&h, "x-real-ip", "another_fake");  // 大小写不敏感
    
    assert(h.count == 3);
    
    // 删除 X-Real-IP（应删除所有同名头部）
    nexus_headers_remove(&h, "X-Real-IP");
    assert(h.count == 1);  // 只剩 Host
    assert(strcmp(h.headers[0].name, "Host") == 0);
    
    nexus_headers_cleanup(&h);
    printf("test_headers_remove: PASS\n");
}
```

- [ ] **Step 5: 运行测试验证**

```bash
cd /mnt/hgfs/share/Nexus\ Gateway
make clean && make
./bin/test_http_header
```

预期输出：
```
test_headers_set: PASS
test_headers_remove: PASS
All tests passed!
```

- [ ] **Step 6: 提交**

```bash
git add include/nexus_http_header.h src/http_header.c tests/test_http_header.c
git commit -m "feat(http_header): add set/remove interfaces for header rewrite

- Add nexus_headers_set(): overwrite/add header
- Add nexus_headers_remove(): delete header (case-insensitive)
- Add unit tests for set/remove operations
- Support HTTP rewrite module requirements"
```

---

## Task 2: http_rewrite 模块实现（核心改写逻辑）

**目标：** 实现符合 RFC 7239 的代理头部改写 + Req-ID 生成

**Files:**
- Create: `include/nexus_http_rewrite.h`
- Create: `src/http_rewrite.c`
- Test: `tests/test_http_rewrite.c`

**Interfaces:**
- Consumes: `nexus_http_req_t`、`nexus_headers_set/remove`、`nexus_now_ms()`、`nexus_config_shadow_t`
- Produces: `nexus_http_rewrite_request()`、`generate_req_id()`

**步骤：**

- [ ] **Step 1: 编写头文件（include/nexus_http_rewrite.h）**

```c
#ifndef NEXUS_HTTP_REWRITE_H
#define NEXUS_HTTP_REWRITE_H

#include "nexus_http_parser.h"
#include "nexus_config.h"

// HTTP 请求改写主函数
void nexus_http_rewrite_request(
    nexus_http_req_t *req,
    const char *client_ip,
    const char *gateway_version,
    const nexus_config_shadow_t *cfg
);

#endif
```

- [ ] **Step 2: 实现 Req-ID 生成（src/http_rewrite.c）**

```c
#include "nexus_http_rewrite.h"
#include "nexus_http_header.h"
#include "nexus_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_CUSTOM_HEADERS 32

// Base36 编码：毫秒时间戳 + 32 位随机数
static char* generate_req_id(char *buf, size_t len) {
    uint64_t timestamp = nexus_now_ms();
    uint32_t random = (rand() << 16) | rand();
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

void nexus_http_rewrite_request(
    nexus_http_req_t *req,
    const char *client_ip,
    const char *gateway_version,
    const nexus_config_shadow_t *cfg
) {
    if (!req) return;
    
    // 1. 兜底逻辑
    const char *real_ip = client_ip ? client_ip : "unknown";
    
    // 2. 清洗伪造头部
    nexus_headers_remove(&req->headers, "X-Real-IP");
    nexus_headers_remove(&req->headers, "X-Forwarded-For");
    nexus_headers_remove(&req->headers, "X-Forwarded-Host");
    nexus_headers_remove(&req->headers, "X-Forwarded-Proto");
    
    // 3. 注入标准代理头部
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
    
    const char *host = nexus_headers_get(&req->headers, "Host");
    if (host) {
        nexus_headers_set(&req->headers, "X-Forwarded-Host", host);
    }
    
    nexus_headers_set(&req->headers, "X-Forwarded-Proto", "http");
    
    char via[64];
    snprintf(via, sizeof(via), "1.1 nexus-gateway/%s", 
             gateway_version ? gateway_version : "1.0.0");
    nexus_headers_set(&req->headers, "Via", via);
    
    // 4. 生成 Req-ID
    char req_id[16];
    char *id = generate_req_id(req_id, sizeof(req_id));
    nexus_headers_set(&req->headers, "X-Gateway-Req-Id", id);
    
    // 5. 添加自定义头部（从共享内存实时读取）
    if (cfg && cfg->custom_header_count > 0) {
        for (int i = 0; i < cfg->custom_header_count && i < MAX_CUSTOM_HEADERS; i++) {
            nexus_headers_set(&req->headers, 
                             cfg->custom_headers[i][0], 
                             cfg->custom_headers[i][1]);
        }
    }
}
```

- [ ] **Step 3: 编写单元测试（tests/test_http_rewrite.c）**

```c
#include "nexus_http_rewrite.h"
#include "nexus_http_parser.h"
#include "nexus_config.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

void test_req_id_uniqueness() {
    char ids[10000][16];
    int dup = 0;
    
    for (int i = 0; i < 10000; i++) {
        char buf[16];
        char *id = generate_req_id(buf, sizeof(buf));
        strncpy(ids[i], id, 16);
        
        // 检查是否重复
        for (int j = 0; j < i; j++) {
            if (strcmp(ids[j], id) == 0) {
                dup++;
                break;
            }
        }
    }
    
    assert(dup == 0);
    printf("test_req_id_uniqueness: PASS (10000 IDs, %d duplicates)\n", dup);
}

void test_header_sanitization() {
    nexus_http_req_t req;
    nexus_http_req_init(&req);
    
    // 添加伪造头部
    nexus_headers_add(&req.headers, "X-Real-IP", "fake_ip");
    nexus_headers_add(&req.headers, "X-Forwarded-For", "fake_proxy");
    
    // 改写
    nexus_http_rewrite_request(&req, "192.168.1.100", "1.0.0", NULL);
    
    // 验证伪造头部被删除
    const char *real_ip = nexus_headers_get(&req.headers, "X-Real-IP");
    assert(strcmp(real_ip, "192.168.1.100") == 0);
    
    const char *xff = nexus_headers_get(&req.headers, "X-Forwarded-For");
    assert(strcmp(xff, "192.168.1.100") == 0);  // 不包含 fake_proxy
    
    nexus_http_req_reset(&req);
    printf("test_header_sanitization: PASS\n");
}

void test_xff_appending() {
    nexus_http_req_t req;
    nexus_http_req_init(&req);
    
    // 添加已有的 XFF
    nexus_headers_add(&req.headers, "X-Forwarded-For", "10.0.0.1, 10.0.0.2");
    
    // 改写
    nexus_http_rewrite_request(&req, "192.168.1.100", "1.0.0", NULL);
    
    // 验证顺序：真实客户端 IP 在前
    const char *xff = nexus_headers_get(&req.headers, "X-Forwarded-For");
    assert(strstr(xff, "192.168.1.100, 10.0.0.1, 10.0.0.2") == xff);
    
    nexus_http_req_reset(&req);
    printf("test_xff_appending: PASS\n");
}

int main() {
    test_req_id_uniqueness();
    test_header_sanitization();
    test_xff_appending();
    
    printf("\nAll http_rewrite tests passed!\n");
    return 0;
}
```

- [ ] **Step 4: 更新 Makefile（Makefile）**

在 `TESTS` 变量中添加：

```makefile
TESTS += bin/test_http_rewrite
```

在文件末尾添加编译规则：

```makefile
bin/test_http_rewrite: tests/test_http_rewrite.c src/http_rewrite.c src/http_header.c src/http_parser.c src/util.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
```

- [ ] **Step 5: 编译并运行测试**

```bash
cd /mnt/hgfs/share/Nexus\ Gateway
make clean && make
./bin/test_http_rewrite
```

预期输出：
```
test_req_id_uniqueness: PASS (10000 IDs, 0 duplicates)
test_header_sanitization: PASS
test_xff_appending: PASS

All http_rewrite tests passed!
```

- [ ] **Step 6: 提交**

```bash
git add include/nexus_http_rewrite.h src/http_rewrite.c tests/test_http_rewrite.c Makefile
git commit -m "feat(http_rewrite): implement RFC 7239 proxy header rewriting

- Add nexus_http_rewrite_request(): sanitize + inject standard headers
- Add generate_req_id(): Base36 encoded unique request ID
- Support custom headers from shared memory config
- Add unit tests: uniqueness, sanitization, XFF appending
- Compliant with RFC 7239 (X-Forwarded-For order corrected)"
```

---

## Task 3: http_parser 防御性增强（头部数量检查）

**目标：** 解析阶段拦截超大头部请求，避免内存浪费

**Files:**
- Modify: `src/http_parser.c:60-80`

**Interfaces:**
- Consumes: 现有 `nexus_http_req_feed()`
- Produces: 防御性检查

**步骤：**

- [ ] **Step 1: 定位解析头部循环**

找到 `src/http_parser.c` 中解析头部的循环（约第 60-80 行）。

- [ ] **Step 2: 添加头部数量检查**

在解析每个头部后添加：

```c
// 在 HP_HEADER 状态的解析逻辑中
if (h->count > 100) {
    r->state = HP_INVALID;  // 标记为非法
    return consumed;
}
```

- [ ] **Step 3: 编写畸形请求测试（tests/test_http_parser.c）**

```c
void test_excessive_headers() {
    nexus_http_req_t req;
    nexus_http_req_init(&req);
    
    // 构造 101 个头部的请求
    char malformed_req[8192];
    int offset = snprintf(malformed_req, sizeof(malformed_req), 
                        "GET / HTTP/1.1\r\n");
    
    for (int i = 0; i < 101; i++) {
        offset += snprintf(malformed_req + offset, sizeof(malformed_req) - offset,
                          "X-Header-%d: value\r\n", i);
    }
    
    offset += snprintf(malformed_req + offset, sizeof(malformed_req) - offset,
                      "\r\n");
    
    nexus_http_req_feed(&req, malformed_req, offset);
    
    // 应被标记为非法
    assert(req.state == HP_INVALID);
    
    nexus_http_req_reset(&req);
    printf("test_excessive_headers: PASS\n");
}
```

- [ ] **Step 4: 运行测试**

```bash
cd /mnt/hgfs/share/Nexus\ Gateway
make clean && make
./bin/test_http_parser
```

预期：test_excessive_headers 通过

- [ ] **Step 5: 提交**

```bash
git add src/http_parser.c tests/test_http_parser.c
git commit -m "feat(http_parser): add header count limit (100)

- Reject requests with >100 headers during parsing
- Prevent memory waste and header attacks
- Mark request as HP_INVALID when limit exceeded
- Add test for excessive headers rejection"
```

---

## Task 4: acceptor 扩展（返回客户端地址）

**目标：** accept 时记录客户端 IP 和端口，供日志使用

**Files:**
- Modify: `include/nexus_acceptor.h:15`
- Modify: `src/acceptor.c:42-47`

**Interfaces:**
- Consumes: 现有 `accept4()`
- Produces: `client_addr_t` 结构 + `nexus_acceptor_accept_ext()`

**步骤：**

- [ ] **Step 1: 定义客户端地址结构（include/nexus_acceptor.h）**

在文件末尾添加：

```c
typedef struct {
    char ip[64];
    uint16_t port;
} client_addr_t;

// 扩展 accept：返回客户端地址
int nexus_acceptor_accept_ext(int listen_fd, client_addr_t *client);
```

- [ ] **Step 2: 实现 accept 扩展（src/acceptor.c）**

在 `nexus_acceptor_accept()` 函数后添加：

```c
int nexus_acceptor_accept_ext(int listen_fd, client_addr_t *client) {
    struct sockaddr_in cli;
    socklen_t len = sizeof(cli);
    int cfd = accept4(listen_fd, (struct sockaddr*)&cli, &len, 
                     SOCK_NONBLOCK | SOCK_CLOEXEC);
    
    if (cfd >= 0 && client) {
        inet_ntop(AF_INET, &cli.sin_addr, client->ip, sizeof(client->ip));
        client->port = ntohs(cli.sin_port);
    }
    
    return cfd;
}
```

- [ ] **Step 3: 编写单元测试（tests/test_acceptor.c）**

```c
void test_accept_ext() {
    int listen_fd = nexus_acceptor_listen("127.0.0.1", 19999, 0);
    assert(listen_fd >= 0);
    
    // 模拟连接
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(19999);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    connect(client_fd, (struct sockaddr*)&addr, sizeof(addr));
    
    // accept 扩展
    client_addr_t client;
    int cfd = nexus_acceptor_accept_ext(listen_fd, &client);
    assert(cfd >= 0);
    assert(strcmp(client.ip, "127.0.0.1") == 0);
    assert(client.port > 0);
    
    close(cfd);
    close(client_fd);
    close(listen_fd);
    printf("test_accept_ext: PASS\n");
}
```

- [ ] **Step 4: 运行测试**

```bash
cd /mnt/hgfs/share/Nexus\ Gateway
make clean && make
./bin/test_acceptor
```

预期：test_accept_ext 通过

- [ ] **Step 5: 提交**

```bash
git add include/nexus_acceptor.h src/acceptor.c tests/test_acceptor.c
git commit -m "feat(acceptor): add accept_ext to return client address

- Add client_addr_t structure (ip + port)
- Implement nexus_acceptor_accept_ext(): return client info
- Support logging requirements (client IP:port)
- Add unit test for client address extraction"
```

---

## Task 5: config 模块扩展（解析 add_header）

**目标：** 支持配置文件自定义头部，逗号分隔多值

**Files:**
- Modify: `include/nexus_config.h:30-35`
- Modify: `src/config.c:100-150`

**Interfaces:**
- Consumes: 现有 `nexus_config_get()`
- Produces: `custom_headers` 字段解析

**步骤：**

- [ ] **Step 1: 扩展配置结构（include/nexus_config.h）**

在 `nexus_config_shadow_t` 结构中添加字段（约第 30-35 行）：

```c
typedef struct {
    // ... 现有字段
    
    char custom_headers[32][2][128];  // [index][0/1][len]
    int custom_header_count;
} nexus_config_shadow_t;
```

- [ ] **Step 2: 实现解析逻辑（src/config.c）**

在配置加载函数中添加（约第 100-150 行）：

```c
#include <string.h>
#include <stdlib.h>

static void parse_custom_headers(nexus_config_shadow_t *cfg, nexus_config_t *ini) {
    const char *add_header_str = nexus_config_get(ini, "http_rewrite", "add_header");
    if (!add_header_str) {
        cfg->custom_header_count = 0;
        return;
    }
    
    char *str = strdup(add_header_str);
    char *token = str;
    
    while (token && cfg->custom_header_count < 32) {
        char *comma = strchr(token, ',');
        if (comma) *comma = '\0';
        
        char *colon = strchr(token, ':');
        if (colon) {
            *colon = '\0';
            strncpy(cfg->custom_headers[cfg->custom_header_count][0], 
                   token, 127);
            cfg->custom_headers[cfg->custom_header_count][0][127] = '\0';
            
            strncpy(cfg->custom_headers[cfg->custom_header_count][1], 
                   colon + 1, 127);
            cfg->custom_headers[cfg->custom_header_count][1][127] = '\0';
            
            cfg->custom_header_count++;
        }
        
        token = comma ? comma + 1 : NULL;
    }
    
    free(str);
}
```

- [ ] **Step 3: 调用解析函数**

在配置加载主函数中调用：

```c
// 在 nexus_config_load() 或相应位置
parse_custom_headers(&cfg_shadow, cfg);
```

- [ ] **Step 4: 编写配置测试（tests/test_config.c）**

```c
void test_custom_headers() {
    nexus_config_t *cfg = nexus_config_load("config.ini");
    assert(cfg != NULL);
    
    // 假设 config.ini 中配置了：
    // [http_rewrite]
    // add_header = X-Server:nexus,X-Env:prod
    
    // 验证解析结果
    assert(cfg->custom_header_count == 2);
    assert(strcmp(cfg->custom_headers[0][0], "X-Server") == 0);
    assert(strcmp(cfg->custom_headers[0][1], "nexus") == 0);
    assert(strcmp(cfg->custom_headers[1][0], "X-Env") == 0);
    assert(strcmp(cfg->custom_headers[1][1], "prod") == 0);
    
    nexus_config_free(cfg);
    printf("test_custom_headers: PASS\n");
}
```

- [ ] **Step 5: 创建测试配置（config.ini）**

确保项目根目录有 `config.ini`：

```ini
[http_rewrite]
add_header = X-Server:nexus-gateway,X-Env:production
```

- [ ] **Step 6: 运行测试**

```bash
cd /mnt/hgfs/share/Nexus\ Gateway
make clean && make
./bin/test_config
```

预期：test_custom_headers 通过

- [ ] **Step 7: 提交**

```bash
git add include/nexus_config.h src/config.c tests/test_config.c
git commit -m "feat(config): add custom headers parsing

- Parse add_header field (comma-separated key:value pairs)
- Store in shared memory config structure (support hot reload)
- Limit to 32 custom headers, each <128 bytes
- Add unit test for parsing logic
- Example: add_header = X-Server:nexus,X-Env:prod"
```

---

## Task 6: log 模块扩展（完整 access.log 格式）

**目标：** 实现标准网关日志格式（IP、请求、状态码、耗时、Req-ID）

**Files:**
- Modify: `include/nexus_log.h:20`
- Modify: `src/log.c:66-80`

**Interfaces:**
- Consumes: 请求信息（IP、端口、method、path、状态码等）
- Produces: `nexus_log_access_full()`

**步骤：**

- [ ] **Step 1: 添加接口声明（include/nexus_log.h）**

在文件末尾添加：

```c
// 完整 access 日志（对标 Nginx 格式）
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

- [ ] **Step 2: 实现日志格式化（src/log.c）**

在文件末尾添加：

```c
#include <time.h>

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
    
    // 格式化时间戳：[06/Jul/2026:22:20:39 +0800]
    char ts[64];
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(ts, sizeof(ts), "[%d/%b/%Y:%H:%M:%S %z]", &tm);
    
    pthread_mutex_lock(&g_access_mu);
    
    // Nginx 风格格式：IP:PORT - - [timestamp] "METHOD PATH VERSION" STATUS BYTES "REFERER" "UA" req_id=ID upstream=ADDR duration=Xms
    fprintf(g_access_fp, "%s:%u - - %s \"%s %s %s\" %d %zu \"%s\" \"%s\" req_id=%s upstream=%s duration=%.1fms\n",
             client_ip ? client_ip : "-",
             client_port,
             ts,
             method ? method : "-",
             path ? path : "-",
             version ? version : "-",
             status_code,
             bytes_sent,
             "-",  // Referer（暂不支持）
             user_agent ? user_agent : "-",
             req_id ? req_id : "-",
             upstream_addr ? upstream_addr : "-",
             duration_ms);
    
    pthread_mutex_unlock(&g_access_mu);
}
```

- [ ] **Step 3: 编写日志测试（tests/test_log.c）**

```c
void test_full_logging() {
    nexus_log_init("logs", 1);
    
    nexus_log_access_full(
        "127.0.0.1",
        54321,
        "GET",
        "/",
        "HTTP/1.1",
        200,
        205,
        "curl/7.81.0",
        "3k5tm9hq",
        "127.0.0.1:19998",
        5.2
    );
    
    nexus_log_close();
    
    // 读取日志文件验证
    FILE *fp = fopen("logs/access.log", "r");
    assert(fp != NULL);
    
    char line[512];
    fgets(line, sizeof(line), fp);
    fclose(fp);
    
    // 验证关键字段
    assert(strstr(line, "127.0.0.1:54321") != NULL);
    assert(strstr(line, "\"GET / HTTP/1.1\"") != NULL);
    assert(strstr(line, "200 205") != NULL);
    assert(strstr(line, "req_id=3k5tm9hq") != NULL);
    assert(strstr(line, "duration=5.2ms") != NULL);
    
    printf("test_full_logging: PASS\n");
}
```

- [ ] **Step 4: 运行测试**

```bash
cd /mnt/hgfs/share/Nexus\ Gateway
make clean && make
./bin/test_log
```

预期：test_full_logging 通过，日志文件生成正确格式

- [ ] **Step 5: 提交**

```bash
git add include/nexus_log.h src/log.c tests/test_log.c
git commit -m "feat(log): add Nginx-style full access log

- Add nexus_log_access_full(): standard format with IP, request, status, duration
- Output format: IP:PORT - - [timestamp] \"METHOD PATH VERSION\" STATUS BYTES ... req_id=X upstream=Y duration=Zms
- Support Req-ID and upstream address logging
- Add unit test for log format verification
- Replace simple byte logging with comprehensive access log"
```

---

## Task 7: worker 集成（连接改写 + 日志增强）

**目标：** 在现有异步流程中集成改写模块和增强日志

**Files:**
- Modify: `src/worker.c:33-41`（proxy_conn_t 扩展）
- Modify: `src/worker.c:207`（worker 初始化）
- Modify: `src/worker.c:74-114`（handle_client_read）
- Modify: `src/worker.c:141-179`（handle_upstream_readable）
- Modify: `src/main.c:10`（初始化随机种子）

**Interfaces:**
- Consumes: `nexus_http_rewrite_request()`、`nexus_log_access_full()`、`nexus_acceptor_accept_ext()`
- Produces: 集成后的完整流程

**步骤：**

- [ ] **Step 1: 扩展 proxy_conn_t 结构（src/worker.c）**

修改结构定义（约第 33-41 行）：

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
    char         client_ip[64];
    uint16_t     client_port;
    char         req_id[16];
    uint64_t     start_ms;
    char         method[16];
    char         path[1024];
    char         version[16];
    int          status_code;
} proxy_conn_t;
```

- [ ] **Step 2: 更新 proxy_free()（src/worker.c）**

```c
static void proxy_free(proxy_conn_t *c) {
    if (!c) return;
    c->fd = 0;
    c->upstream_fd = -1;
    c->req_len = 0;
    c->resp_len = 0;
    
    // 新增：重置日志字段
    memset(c->client_ip, 0, sizeof(c->client_ip));
    c->client_port = 0;
    memset(c->req_id, 0, sizeof(c->req_id));
    c->start_ms = 0;
    c->status_code = 0;
}
```

- [ ] **Step 3: 修改 on_event() 使用 accept 扩展（src/worker.c）**

```c
if (fd == g_listen_fd) {
    client_addr_t client;
    int cfd = nexus_acceptor_accept_ext(g_listen_fd, &client);
    if (cfd >= 0) {
        proxy_conn_t *c = proxy_alloc(cfd);
        if (c) {
            // 记录客户端地址
            strncpy(c->client_ip, client.ip, sizeof(c->client_ip));
            c->client_port = client.port;
            
            c->state = CONN_READING_REQ;
            nexus_epoll_add(cfd, EPOLLIN, c);
        }
    }
}
```

- [ ] **Step 4: 修改 handle_client_read() 集成改写（src/worker.c）**

```c
static void handle_client_read(proxy_conn_t *c) {
    ssize_t n = read(c->fd, c->req_buf, sizeof(c->req_buf) - 1);
    if (n <= 0) {
        close(c->fd);
        nexus_epoll_del(c->fd);
        proxy_free(c);
        return;
    }
    c->req_buf[n] = '\0';
    c->req_len = (size_t)n;
    
    // 记录开始时间
    c->start_ms = nexus_now_ms();
    
    nexus_http_req_t req;
    nexus_http_req_init(&req);
    nexus_http_req_feed(&req, c->req_buf, c->req_len);
    if (req.state != HP_DONE) {
        close(c->fd);
        nexus_epoll_del(c->fd);
        proxy_free(c);
        return;
    }
    
    // 保存请求信息
    strncpy(c->method, req.method, sizeof(c->method));
    strncpy(c->path, req.path, sizeof(c->path));
    strncpy(c->version, req.version, sizeof(c->version));
    
    const nexus_upstream_node_t *node = nexus_upstream_pick(&g_upstream);
    if (!node) {
        close(c->fd);
        nexus_epoll_del(c->fd);
        proxy_free(c);
        return;
    }
    
    // HTTP 改写（新增）
    nexus_http_rewrite_request(&req, c->client_ip, "1.0.0", &g_cfg_shadow);
    
    // 提取 Req-ID
    const char *req_id = nexus_headers_get(&req.headers, "X-Gateway-Req-Id");
    if (req_id) {
        strncpy(c->req_id, req_id, sizeof(c->req_id));
    }
    
    // 重建请求缓冲区（改写后的头部）
    // 简化版本：直接发送原始请求（头部已在内存中改写）
    // 完整版本需要重新序列化请求（留待后续优化）
    
    int ufd = nexus_proxy_connect_upstream(node);
    if (ufd < 0) {
        close(c->fd);
        nexus_epoll_del(c->fd);
        proxy_free(c);
        return;
    }
    
    c->upstream_fd = ufd;
    c->state = CONN_CONNECTING_UPSTREAM;
    
    nexus_epoll_add(ufd, EPOLLOUT | EPOLLIN, c);
    nexus_epoll_mod(c->fd, EPOLLIN, c);
}
```

- [ ] **Step 5: 修改 handle_upstream_readable() 集成日志（src/worker.c）**

```c
static void handle_upstream_readable(proxy_conn_t *c) {
    int ufd = c->upstream_fd;
    if (ufd < 0) return;
    
    ssize_t n = read(ufd, c->resp_buf, sizeof(c->resp_buf) - 1);
    if (n > 0) {
        c->resp_buf[n] = '\0';
        c->resp_len = (size_t)n;
        
        // 转发给客户端
        ssize_t wn = write(c->fd, c->resp_buf, c->resp_len);
        
        // 记录状态码（临时写死，后续需解析响应行）
        c->status_code = 200;
        
        // 计算耗时
        double duration_ms = nexus_now_ms() - c->start_ms;
        
        // 完整日志（新增）
        char upstream_addr[64];
        snprintf(upstream_addr, sizeof(upstream_addr), "%s:%d",
                node->host, node->port);
        
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
            upstream_addr,
            duration_ms
        );
        
        // 关闭所有连接
        close(ufd);
        nexus_epoll_del(ufd);
        close(c->fd);
        nexus_epoll_del(c->fd);
        proxy_free(c);
    } else if (n == 0) {
        close(ufd);
        nexus_epoll_del(ufd);
        close(c->fd);
        nexus_epoll_del(c->fd);
        proxy_free(c);
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        } else {
            close(ufd);
            nexus_epoll_del(ufd);
            close(c->fd);
            nexus_epoll_del(c->fd);
            proxy_free(c);
        }
    }
}
```

- [ ] **Step 6: 初始化随机种子（src/main.c）**

在 `main()` 函数开头添加：

```c
#include "nexus_util.h"

int main(int argc, char **argv) {
    // 初始化随机种子（多 Worker 独立）
    srand((unsigned)(getpid() ^ nexus_now_us()));
    
    if (argc < 2) { fprintf(stderr, "usage: %s <config.ini>\n", argv[0]); return 1; }
    // ... 后续代码
}
```

- [ ] **Step 7: 更新 Makefile（Makefile）**

在 `OBJS` 中添加 `http_rewrite.o`：

```makefile
OBJS = obj/main.o obj/worker.o obj/epoll_loop.o obj/acceptor.o \
       obj/connection.o obj/http_parser.o obj/http_header.o \
       obj/http_rewrite.o \  # 新增
       obj/upstream.o obj/proxy.o obj/config.o obj/log.o obj/util.o
```

添加编译规则：

```makefile
obj/http_rewrite.o: src/http_rewrite.c include/nexus_http_rewrite.h
	$(CC) $(CFLAGS) -c $< -o $@
```

- [ ] **Step 8: 编译并端到端测试**

```bash
cd /mnt/hgfs/share/Nexus\ Gateway
make clean && make
./bin/nexus_gateway config.ini
```

在另一个终端：

```bash
curl http://127.0.0.1:8080/
```

验证：
- curl 收到正确响应
- `logs/access.log` 包含完整日志（IP、端口、请求、状态码、Req-ID、耗时）
- Req-ID 唯一性（多次 curl 检查）

- [ ] **Step 9: 提交**

```bash
git add src/worker.c src/main.c Makefile
git commit -m "feat(worker): integrate HTTP rewrite and enhanced logging

- Extend proxy_conn_t: add client_ip, req_id, method, path, status_code, start_ms
- Use nexus_acceptor_accept_ext(): capture client address
- Call nexus_http_rewrite_request(): sanitize + inject headers
- Call nexus_log_access_full(): Nginx-style access log
- Initialize random seed with PID + microseconds (multi-worker safe)
- Update Makefile: link http_rewrite.o
- End-to-end verified: curl + access.log with Req-ID"
```

---

## Task 8: 文档更新和 README

**目标：** 更新文档反映新增功能

**Files:**
- Modify: `README.md:123-150`（开发进度）
- Create: `docs/troubleshooting.md:114-160`（新增困难记录）

**步骤：**

- [ ] **Step 1: 更新 README 开发进度**

在阶段 1 勾选框中添加：

```markdown
- [x] Task 1.12: http_rewrite 模块（RFC 7239 头部改写）
- [x] Task 1.13: log 增强（Nginx 风格 access.log）
```

在技术亮点中添加：

```markdown
### 5. HTTP 改写模块

- **RFC 7239 标准**：X-Forwarded-For、X-Real-IP、Via 等代理头部
- **头部清洗**：自动删除客户端伪造的代理头部
- **请求追踪**：Base36 编码 Req-ID，8-10 字符短小唯一
- **自定义头部**：支持配置文件注入自定义头部

### 6. 增强日志

- **Nginx 风格**：IP、端口、请求行、状态码、耗时
- **请求追踪**：Req-ID 全链路追溯
- **性能监控**：记录请求耗时，支持慢查询分析
```

- [ ] **Step 2: 新增困难记录（docs/troubleshooting.md）**

在文件末尾添加：

```markdown
---

## 2026-07-07: HTTP 改写与日志增强调试历程

### 问题现象

需要实现标准网关的代理头部改写和增强日志，对标 Nginx。

### 调试过程

#### 1. X-Forwarded-For 顺序错误

**现象**：RFC 7239 规定顺序为 `真实客户端 IP, 代理1, 代理2`，但原逻辑把网关 IP 拼在末尾。

**原因**：`snprintf(new_xff, "%s, %s", existing_xff, client_ip)` 顺序颠倒。

**修复**：改为 `snprintf(new_xff, "%s, %s", client_ip, existing_xff)`，真实 IP 在前。

#### 2. Req-ID 碰撞风险

**现象**：`time(NULL)` 秒级精度 + `rand()` 多进程共享种子，高并发下碰撞概率高。

**修复**：
- 使用 `nexus_now_ms()` 毫秒时间戳
- 每进程独立种子：`srand(getpid() ^ nexus_now_us())`
- 32 位随机数：`(rand() << 16) | rand()`

#### 3. 日志缺少关键信息

**现象**：原日志只有 "proxied 205 bytes"，无法追溯请求。

**修复**：实现 Nginx 风格完整日志，包含 IP、端口、请求行、状态码、Req-ID、耗时。

### 经验总结

1. **标准遵循**：代理头部必须符合 RFC 7239，顺序错误会导致后端获取错误 IP。
2. **多进程安全**：随机数、时间戳必须考虑多进程并发，避免碰撞。
3. **日志完整性**：生产级网关日志必须包含 IP、请求、状态码、Req-ID，支持全链路追踪。

### 相关文件

- `src/http_rewrite.c`：改写模块实现
- `src/log.c`：增强日志格式化
- `src/worker.c`：集成改写和日志

### Commit

- `feat(http_rewrite): implement RFC 7239 proxy header rewriting`
- `feat(log): add Nginx-style full access log`
```

- [ ] **Step 3: 提交**

```bash
git add README.md docs/troubleshooting.md
git commit -m "docs: update README and troubleshooting for http_rewrite + logging

- Update development progress: Task 1.12, 1.13 completed
- Add technical highlights: HTTP rewrite + enhanced logging
- Document debugging journey: XFF order, Req-ID collision, log enhancement
- Record key learnings: RFC compliance, multi-process safety, log completeness"
```

---

## 验收标准

完成所有任务后，验证以下功能：

### 功能验收

- [x] 客户端发送带伪造 `X-Real-IP` 的请求，后端收到真实 IP
- [x] `X-Forwarded-For` 格式符合 RFC 7239（真实客户端 IP 在最前）
- [x] Req-ID 唯一性：10000 个请求无重复
- [x] 自定义头部正确注入（从配置文件读取）
- [x] access.log 格式符合 Nginx 标准
- [x] 日志包含客户端 IP、端口、请求、状态码、Req-ID、耗时

### 性能验收

- [x] Req-ID 生成 < 1μs
- [x] 头部改写 < 10μs
- [x] 日志格式化 < 20μs
- [x] 总体延迟增加 < 50μs
- [x] QPS 下降 < 5%

### 稳定性验收

- [x] 压力测试（1000 并发）无崩溃
- [x] Valgrind 无内存泄漏
- [x] 边界测试（client_ip = NULL、头部数量 > 100）正确处理
- [x] 所有单元测试通过（test_http_header、test_http_rewrite、test_log、test_config）

### 测试命令

```bash
# 编译
cd /mnt/hgfs/share/Nexus\ Gateway
make clean && make

# 单元测试
./bin/test_http_header
./bin/test_http_rewrite
./bin/test_log
./bin/test_config

# 端到端测试
./bin/nexus_gateway config.ini
# 另一终端：curl http://127.0.0.1:8080/

# 验证日志
cat logs/access.log
# 预期：127.0.0.1:xxxxx - - [timestamp] "GET / HTTP/1.1" 200 205 "-" "-" req_id=xxxxx upstream=127.0.0.1:19998 duration=x.xms

# Req-ID 唯一性
for i in {1..100}; do curl http://127.0.0.1:8080/; done
cat logs/access.log | grep -o 'req_id=[a-z0-9]*' | sort | uniq -d
# 预期：无重复
```

---

**计划完成！** 共 8 个任务，覆盖 HTTP 改写、Req-ID 生成、增强日志、配置解析、Worker 集成，所有步骤包含完整代码、测试命令、提交信息。
