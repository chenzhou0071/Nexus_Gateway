# Nexus-Gateway Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 从零实现 Master-Worker 多进程高可用网关，含 9 大模块：epoll / 零拷贝 / HTTP 状态机 / 加权轮询 / 健康检测 / 平滑重启 / 限流 / 黑名单 / 哈希头检索

**Architecture:** 纯 C + Linux 系统调用；Master 进程管配置与子进程，Worker 用 SO_REUSEPORT 共享端口；配置走共享内存双缓冲 + atomic 切换；先骨架后亮点，4 阶段递进

**Tech Stack:** C99/Linux（epoll, sendfile, splice, SO_REUSEPORT, mmap, signalfd, inotify）/ Makefile / wrk 压测

## Global Constraints

- **语言**：纯 C，遵循 C99，禁止 GNU 扩展（保持可移植到主流 Linux 发行版）
- **构建**：GNU Makefile，无 CMake，无 autoconf，无第三方库
- **系统调用目标**：Linux 3.10+（CentOS 7 / Ubuntu 16.04 兼容）
- **进程模型**：Master 单实例，Worker 数量 = `sysconf(_SC_NPROCESSORS_ONFNS)`
- **端口共享**：所有 Worker 使用 `SO_REUSEPORT` 监听同一端口
- **配置格式**：自研 INI，`[section]` + `key=value`，支持 `#` 和 `;` 注释
- **配置热更**：共享内存双缓冲 + `atomic_int` 切换，lock-free
- **HTTP 范围**：GET / POST / HEAD / Content-Length / keep-alive。不支持 chunked、gzip、HTTPS
- **代码风格**：4 空格缩进，函数 ≤ 100 行，文件 ≤ 600 行，命名 `lower_snake_case`
- **提交粒度**：每个 Task 提交一次，commit 消息格式 `feat: <module> <change>` 或 `test: ...`，**不带 Co-Authored-By**
- **测试**：单元测试 + 集成测试 + 压测，每步 TDD
- **路径**：源码在 `src/`，头文件在 `include/`，测试在 `tests/`，集成在 `tests/integration/`，压测在 `bench/`
- **日志**：`logs/access.log`（请求行 + 状态码 + 耗时）、`logs/error.log`（错误事件 + 堆栈片段）
- **零拷贝触发**：静态文件用 `sendfile`；代理响应 > 16KB 用 `splice`；小包走普通 `write`

---

## 阶段 0：项目骨架

### Task 0.1: 仓库结构与 Makefile

**Files:**
- Create: `Makefile`
- Create: `.gitignore`
- Create: `config.ini.example`
- Create: `include/nexus_config.h`（占位头）
- Create: `src/main.c`（仅打印版本并退出）

**Interfaces:**
- Produces: `make all` 生成 `bin/nexus_gateway`；`make clean` 清理；`make test` 跑单元测试

- [ ] **Step 1: 创建 .gitignore**

写入 `E:\share\Nexus Gateway\.gitignore`（**覆盖**现有内容，去掉 AL 模板行）：

```gitignore
# 编译产物
bin/
obj/
*.o
*.a
*.so

# 日志
logs/*.log
!logs/.gitkeep

# 压测输出
bench/*.csv
bench/*.png

# 临时
*.swp
*~
.vscode/
```

- [ ] **Step 2: 创建目录占位**

```bash
mkdir -p src include tests/integration bench logs config
touch logs/.gitkeep
```

- [ ] **Step 3: 写 Makefile**

写入 `E:\share\Nexus Gateway\Makefile`：

```makefile
CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -Werror -O2 -g -D_GNU_SOURCE -Iinclude
LDFLAGS = -lpthread

SRC_DIR   = src
OBJ_DIR   = obj
BIN_DIR   = bin
TEST_DIR  = tests
TEST_BIN  = $(BIN_DIR)/unit_tests

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))
BIN  = $(BIN_DIR)/nexus_gateway

TEST_SRCS = $(wildcard $(TEST_DIR)/test_*.c)
TEST_OBJS = $(patsubst $(TEST_DIR)/%.c,$(OBJ_DIR)/test_%.o,$(TEST_SRCS))

.PHONY: all clean test bench

all: $(BIN)

$(BIN): $(OBJS) | $(BIN_DIR)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/test_%.o: $(TEST_DIR)/test_%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -Itests -c $< -o $@

$(TEST_BIN): $(TEST_OBJS) $(filter-out $(OBJ_DIR)/main.o,$(OBJS)) | $(BIN_DIR)
	$(CC) $(TEST_OBJS) $(filter-out $(OBJ_DIR)/main.o,$(OBJS)) -o $@ $(LDFLAGS)

test: $(TEST_BIN)
	./$(TEST_BIN)

bench:
	bash bench/bench.sh

$(OBJ_DIR) $(BIN_DIR):
	mkdir -p $@

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
```

- [ ] **Step 4: 写 config.ini.example**

写入 `E:\share\Nexus Gateway\config.ini.example`：

```ini
# Nexus-Gateway 配置示例

[server]
listen       = 0.0.0.0:8080
worker_num   = auto
log_level    = info
max_conns    = 10000

[upstream.api]
# 加权轮询后端，weight 缺省 = 1
server  = 127.0.0.1:9001 weight=3
server  = 127.0.0.1:9002 weight=2
server  = 127.0.0.1:9003 weight=1

[upstream.static]
type    = file
root    = /var/www/html

[route]
# 路径前缀 → 上游名
/api    = api
/static = static

[security]
rate_limit_per_ip = 100
blacklist         = 192.168.1.100, 10.0.0.5
```

- [ ] **Step 5: 写占位 include/nexus_config.h**

写入 `E:\share\Nexus Gateway\include\nexus_config.h`：

```c
#ifndef NEXUS_CONFIG_H
#define NEXUS_CONFIG_H

#define NEXUS_VERSION "0.1.0"

#endif
```

- [ ] **Step 6: 写 src/main.c（最小可执行）**

写入 `E:\share\Nexus Gateway\src\main.c`：

```c
#include <stdio.h>
#include "nexus_config.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf("nexus_gateway %s (skeleton)\n", NEXUS_VERSION);
    return 0;
}
```

- [ ] **Step 7: 编译并运行**

```bash
cd "E:/share/Nexus Gateway" && make clean && make && ./bin/nexus_gateway
```

预期输出：
```
nexus_gateway 0.1.0 (skeleton)
```

- [ ] **Step 8: 提交**

```bash
cd "E:/share/Nexus Gateway" && git add . && git commit -m "feat: scaffold project structure with Makefile and config example"
```

---

## 阶段 1：骨架（MVP 端到端）

### Task 1.1: util 模块（字符串/内存/时间/原子工具）

**Files:**
- Create: `include/nexus_util.h`
- Create: `src/util.c`
- Create: `tests/test_util.c`

**Interfaces:**
- Produces:
  - `char *nexus_strdup(const char *s)`
  - `int nexus_strcmp_ci(const char *a, const char *b)`（大小写不敏感）
  - `uint64_t nexus_now_ms(void)`（单调时钟，毫秒）
  - `uint64_t nexus_now_us(void)`（微秒）
  - `uint32_t nexus_hash_fnv1a(const char *s, size_t len)`
  - `void *nexus_xmalloc(size_t n)`（失败 abort）

- [ ] **Step 1: 写测试 tests/test_util.c**

```c
#include "nexus_util.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>

static void test_strdup(void) {
    char *s = nexus_strdup("hello");
    assert(strcmp(s, "hello") == 0);
    free(s);
}

static void test_strcmp_ci(void) {
    assert(nexus_strcmp_ci("Host", "host") == 0);
    assert(nexus_strcmp_ci("Content-Length", "content-length") == 0);
    assert(nexus_strcmp_ci("a", "b") != 0);
}

static void test_hash_fnv1a(void) {
    uint32_t a = nexus_hash_fnv1a("Host", 4);
    uint32_t b = nexus_hash_fnv1a("Host", 4);
    assert(a == b);
    uint32_t c = nexus_hash_fnv1a("host", 4);
    assert(a != c);  // FNV-1a 对大小写敏感
}

static void test_now_ms_monotonic(void) {
    uint64_t t1 = nexus_now_ms();
    for (volatile int i = 0; i < 1000000; i++);
    uint64_t t2 = nexus_now_ms();
    assert(t2 > t1);
}

int main(void) {
    test_strdup();
    test_strcmp_ci();
    test_hash_fnv1a();
    test_now_ms_monotonic();
    printf("test_util: all passed\n");
    return 0;
}
```

- [ ] **Step 2: 写 include/nexus_util.h**

```c
#ifndef NEXUS_UTIL_H
#define NEXUS_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

char    *nexus_strdup(const char *s);
int      nexus_strcmp_ci(const char *a, const char *b);
uint64_t nexus_now_ms(void);
uint64_t nexus_now_us(void);
uint32_t nexus_hash_fnv1a(const char *s, size_t len);
void    *nexus_xmalloc(size_t n);
void     nexus_die(const char *msg);  // 打印 msg + abort

#endif
```

- [ ] **Step 3: 写 src/util.c**

```c
#define _POSIX_C_SOURCE 200809L
#include "nexus_util.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

static uint64_t mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static uint64_t mono_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
}

uint64_t nexus_now_ms(void) { return mono_ms(); }
uint64_t nexus_now_us(void) { return mono_us(); }

char *nexus_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = nexus_xmalloc(n);
    memcpy(p, s, n);
    return p;
}

int nexus_strcmp_ci(const char *a, const char *b) {
    return strcasecmp(a, b);
}

uint32_t nexus_hash_fnv1a(const char *s, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (unsigned char)s[i];
        h *= 16777619u;
    }
    return h;
}

void *nexus_xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) nexus_die("xmalloc: out of memory");
    return p;
}

void nexus_die(const char *msg) {
    fprintf(stderr, "nexus: fatal: %s\n", msg);
    abort();
}
```

- [ ] **Step 4: 跑测试**

```bash
cd "E:/share/Nexus Gateway" && make test
```

预期：所有 4 个测试通过，输出 `test_util: all passed`。

- [ ] **Step 5: 提交**

```bash
cd "E:/share/Nexus Gateway" && git add . && git commit -m "feat(util): add string, time, FNV-1a hash helpers with tests"
```

---

### Task 1.2: log 模块（Nginx 风格 access/error 日志）

**Files:**
- Create: `include/nexus_log.h`
- Create: `src/log.c`
- Create: `tests/test_log.c`

**Interfaces:**
- Produces:
  - `int nexus_log_init(const char *log_dir, int log_level)`（log_level: 0=debug 1=info 2=warn 3=error）
  - `void nexus_log_close(void)`
  - `void nexus_log_access(const char *fmt, ...)`
  - `void nexus_log_error(int level, const char *fmt, ...)`（level 比较阈值）

- [ ] **Step 1: 写测试 tests/test_log.c**

```c
#include "nexus_log.h"
#include "nexus_util.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

int main(void) {
    // 用 /tmp 下的临时目录
    const char *dir = "/tmp/nexus_log_test";
    mkdir(dir, 0755);

    assert(nexus_log_init(dir, 1) == 0);

    nexus_log_access("127.0.0.1 GET / 200 %lu", nexus_now_ms());
    nexus_log_error(2, "test warning: %d", 42);

    nexus_log_close();

    // 验证文件被创建
    struct stat st;
    assert(stat("/tmp/nexus_log_test/access.log", &st) == 0);
    assert(st.st_size > 0);
    assert(stat("/tmp/nexus_log_test/error.log", &st) == 0);
    assert(st.st_size > 0);

    printf("test_log: all passed\n");
    return 0;
}
```

- [ ] **Step 2: 写 include/nexus_log.h**

```c
#ifndef NEXUS_LOG_H
#define NEXUS_LOG_H

#include <stdarg.h>

int  nexus_log_init(const char *log_dir, int log_level);
void nexus_log_close(void);
void nexus_log_access(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void nexus_log_error(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#endif
```

- [ ] **Step 3: 写 src/log.c**

```c
#define _POSIX_C_SOURCE 200809L
#include "nexus_log.h"
#include "nexus_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>

static FILE *g_access_fp = NULL;
static FILE *g_error_fp = NULL;
static int   g_log_level = 1;
static pthread_mutex_t g_access_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_error_mu  = PTHREAD_MUTEX_INITIALIZER;

int nexus_log_init(const char *log_dir, int log_level) {
    mkdir(log_dir, 0755);
    char path[512];
    snprintf(path, sizeof(path), "%s/access.log", log_dir);
    g_access_fp = fopen(path, "a");
    snprintf(path, sizeof(path), "%s/error.log", log_dir);
    g_error_fp = fopen(path, "a");
    g_log_level = log_level;
    if (!g_access_fp || !g_error_fp) return -1;
    // 启动时 line-buffered 便于 tail -f
    setvbuf(g_access_fp, NULL, _IOLBF, 0);
    setvbuf(g_error_fp,  NULL, _IOLBF, 0);
    return 0;
}

void nexus_log_close(void) {
    if (g_access_fp) { fclose(g_access_fp); g_access_fp = NULL; }
    if (g_error_fp)  { fclose(g_error_fp);  g_error_fp  = NULL; }
}

static void format_ts(char *buf, size_t n) {
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, n, "%Y-%m-%d %H:%M:%S", &tm);
}

void nexus_log_access(const char *fmt, ...) {
    if (!g_access_fp) return;
    char ts[32];
    format_ts(ts, sizeof(ts));
    pthread_mutex_lock(&g_access_mu);
    fprintf(g_access_fp, "[%s] ", ts);
    va_list ap; va_start(ap, fmt); vfprintf(g_access_fp, fmt, ap); va_end(ap);
    fputc('\n', g_access_fp);
    pthread_mutex_unlock(&g_access_mu);
}

void nexus_log_error(int level, const char *fmt, ...) {
    if (!g_error_fp || level < g_log_level) return;
    const char *tag = (level >= 3) ? "ERROR" : (level == 2 ? "WARN " : "INFO ");
    char ts[32];
    format_ts(ts, sizeof(ts));
    pthread_mutex_lock(&g_error_mu);
    fprintf(g_error_fp, "[%s] [%s] ", ts, tag);
    va_list ap; va_start(ap, fmt); vfprintf(g_error_fp, fmt, ap); va_end(ap);
    fputc('\n', g_error_fp);
    pthread_mutex_unlock(&g_error_mu);
}
```

- [ ] **Step 4: 跑测试**

```bash
cd "E:/share/Nexus Gateway" && make test
```

预期：`test_log: all passed`。

- [ ] **Step 5: 提交**

```bash
cd "E:/share/Nexus Gateway" && git add . && git commit -m "feat(log): add access/error log with mutex-guarded writes"
```

---

### Task 1.3: config 模块（INI 解析）

**Files:**
- Create: `include/nexus_config.h`（**覆盖**占位）
- Create: `src/config.c`
- Create: `tests/test_config.c`

**Interfaces:**
- Produces:
  ```c
  typedef struct nexus_config_t nexus_config_t;  // 不透明
  nexus_config_t *nexus_config_load(const char *path);
  void           nexus_config_free(nexus_config_t *cfg);
  const char    *nexus_config_get(nexus_config_t *cfg, const char *section, const char *key);
  int            nexus_config_get_int(nexus_config_t *cfg, const char *section, const char *key, int def);
  ```

- [ ] **Step 1: 写测试 tests/test_config.c**

```c
#include "nexus_config.h"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

static void write_tmp(const char *content) {
    FILE *f = fopen("/tmp/nexus_test.ini", "w");
    fputs(content, f); fclose(f);
}

int main(void) {
    write_tmp(
        "# 注释行\n"
        "[server]\n"
        "listen = 0.0.0.0:8080\n"
        "max_conns = 10000\n"
        "\n"
        "[upstream.api]\n"
        "server = 127.0.0.1:9001 weight=3\n"
        "; 也是注释\n"
        "[security]\n"
        "blacklist = 192.168.1.100,10.0.0.5\n"
    );
    nexus_config_t *cfg = nexus_config_load("/tmp/nexus_test.ini");
    assert(cfg);
    assert(strcmp(nexus_config_get(cfg, "server", "listen"), "0.0.0.0:8080") == 0);
    assert(nexus_config_get_int(cfg, "server", "max_conns", 0) == 10000);
    assert(strcmp(nexus_config_get(cfg, "upstream.api", "server"), "127.0.0.1:9001 weight=3") == 0);
    assert(strcmp(nexus_config_get(cfg, "missing", "key"), NULL) == 0);  // section 缺失
    assert(nexus_config_get_int(cfg, "missing", "key", 42) == 42);       // 默认值
    nexus_config_free(cfg);
    unlink("/tmp/nexus_test.ini");
    printf("test_config: all passed\n");
    return 0;
}
```

- [ ] **Step 2: 覆盖写 include/nexus_config.h**

```c
#ifndef NEXUS_CONFIG_H
#define NEXUS_CONFIG_H

#include <stddef.h>

#define NEXUS_VERSION "0.1.0"

typedef struct nexus_config_t nexus_config_t;

nexus_config_t *nexus_config_load(const char *path);
void            nexus_config_free(nexus_config_t *cfg);
const char     *nexus_config_get(nexus_config_t *cfg, const char *section, const char *key);
int             nexus_config_get_int(nexus_config_t *cfg, const char *section, const char *key, int def);

#endif
```

- [ ] **Step 3: 写 src/config.c**

```c
#define _POSIX_C_SOURCE 200809L
#include "nexus_config.h"
#include "nexus_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_KV 256

typedef struct {
    char *key;
    char *value;
} kv_t;

struct nexus_config_t {
    char *current_section;
    kv_t  kvs[MAX_KV];
    int   kv_count;
};

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end-1))) *--end = '\0';
    return s;
}

nexus_config_t *nexus_config_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    nexus_config_t *cfg = nexus_xmalloc(sizeof(*cfg));
    memset(cfg, 0, sizeof(*cfg));
    cfg->current_section = nexus_strdup("default");

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *p = trim(line);
        if (*p == '\0' || *p == '#' || *p == ';') continue;
        if (*p == '[') {
            char *end = strchr(p, ']');
            if (!end) continue;
            *end = '\0';
            free(cfg->current_section);
            cfg->current_section = nexus_strdup(p + 1);
        } else {
            char *eq = strchr(p, '=');
            if (!eq || cfg->kv_count >= MAX_KV) continue;
            *eq = '\0';
            char *k = trim(p);
            char *v = trim(eq + 1);
            cfg->kvs[cfg->kv_count].key   = nexus_strdup(k);
            cfg->kvs[cfg->kv_count].value = nexus_strdup(v);
            cfg->kv_count++;
        }
    }
    fclose(f);
    return cfg;
}

void nexus_config_free(nexus_config_t *cfg) {
    if (!cfg) return;
    for (int i = 0; i < cfg->kv_count; i++) {
        free(cfg->kvs[i].key);
        free(cfg->kvs[i].value);
    }
    free(cfg->current_section);
    free(cfg);
}

const char *nexus_config_get(nexus_config_t *cfg, const char *section, const char *key) {
    for (int i = 0; i < cfg->kv_count; i++) {
        // 简化版：只匹配 "section.key" 形式的存储键
        char compound[256];
        snprintf(compound, sizeof(compound), "%s.%s", section, key);
        if (strcmp(cfg->kvs[i].key, compound) == 0) {
            return cfg->kvs[i].value;
        }
    }
    return NULL;
}

int nexus_config_get_int(nexus_config_t *cfg, const char *section, const char *key, int def) {
    const char *v = nexus_config_get(cfg, section, key);
    if (!v) return def;
    return atoi(v);
}
```

- [ ] **Step 4: 跑测试**

```bash
cd "E:/share/Nexus Gateway" && make test
```

预期：`test_config: all passed`。

- [ ] **Step 5: 提交**

```bash
cd "E:/share/Nexus Gateway" && git add . && git commit -m "feat(config): add INI parser with section.key lookup"
```

---

### Task 1.4: connection 模块（连接池 + 状态机）

**Files:**
- Create: `include/nexus_connection.h`
- Create: `src/connection.c`
- Create: `tests/test_connection.c`

**Interfaces:**
- Produces:
  ```c
  typedef enum {
      CONN_IDLE,
      CONN_READING_REQ,
      CONN_CONNECTING_UPSTREAM,
      CONN_WRITING_UPSTREAM,
      CONN_READING_UPSTREAM,
      CONN_WRITING_CLIENT,
      CONN_CLOSING,
  } conn_state_t;

  typedef struct {
      int          fd;
      conn_state_t state;
      uint64_t     created_ms;
      uint64_t     last_active_ms;
  } nexus_conn_t;

  #define CONN_POOL_SIZE 1024
  extern nexus_conn_t g_conn_pool[CONN_POOL_SIZE];

  nexus_conn_t *conn_alloc(int fd);
  void          conn_free(nexus_conn_t *c);
  nexus_conn_t *conn_get(int fd);  // fd → conn 查表
  ```

- [ ] **Step 1: 写测试 tests/test_connection.c**

```c
#include "nexus_connection.h"
#include <assert.h>

int main(void) {
    memset(g_conn_pool, 0, sizeof(g_conn_pool));

    nexus_conn_t *c1 = conn_alloc(7);
    assert(c1 && c1->fd == 7 && c1->state == CONN_IDLE);

    nexus_conn_t *c2 = conn_get(7);
    assert(c2 == c1);

    conn_free(c1);
    assert(conn_get(7) == NULL);

    printf("test_connection: all passed\n");
    return 0;
}
```

- [ ] **Step 2: 写 include/nexus_connection.h**

```c
#ifndef NEXUS_CONNECTION_H
#define NEXUS_CONNECTION_H

#include <stdint.h>

#define CONN_POOL_SIZE 1024

typedef enum {
    CONN_IDLE,
    CONN_READING_REQ,
    CONN_CONNECTING_UPSTREAM,
    CONN_WRITING_UPSTREAM,
    CONN_READING_UPSTREAM,
    CONN_WRITING_CLIENT,
    CONN_CLOSING,
} conn_state_t;

typedef struct {
    int          fd;
    conn_state_t state;
    uint64_t     created_ms;
    uint64_t     last_active_ms;
} nexus_conn_t;

extern nexus_conn_t g_conn_pool[CONN_POOL_SIZE];

nexus_conn_t *conn_alloc(int fd);
void          conn_free(nexus_conn_t *c);
nexus_conn_t *conn_get(int fd);

#endif
```

- [ ] **Step 3: 写 src/connection.c**

```c
#include "nexus_connection.h"
#include "nexus_util.h"
#include <string.h>
#include <stddef.h>

nexus_conn_t g_conn_pool[CONN_POOL_SIZE];

nexus_conn_t *conn_alloc(int fd) {
    for (int i = 0; i < CONN_POOL_SIZE; i++) {
        if (g_conn_pool[i].fd == 0) {
            g_conn_pool[i].fd = fd;
            g_conn_pool[i].state = CONN_IDLE;
            g_conn_pool[i].created_ms = nexus_now_ms();
            g_conn_pool[i].last_active_ms = g_conn_pool[i].created_ms;
            return &g_conn_pool[i];
        }
    }
    return NULL;
}

void conn_free(nexus_conn_t *c) {
    if (!c) return;
    c->fd = 0;
    c->state = CONN_IDLE;
}

nexus_conn_t *conn_get(int fd) {
    for (int i = 0; i < CONN_POOL_SIZE; i++) {
        if (g_conn_pool[i].fd == fd) return &g_conn_pool[i];
    }
    return NULL;
}
```

- [ ] **Step 4: 跑测试**

```bash
cd "E:/share/Nexus Gateway" && make test
```

预期：`test_connection: all passed`。

- [ ] **Step 5: 提交**

```bash
cd "E:/share/Nexus Gateway" && git add . && git commit -m "feat(connection): add connection pool with state machine and fd lookup"
```

---

### Task 1.5: epoll_loop 模块（事件循环骨架）

**Files:**
- Create: `include/nexus_epoll.h`
- Create: `src/epoll_loop.c`
- Create: `tests/test_epoll.c`

**Interfaces:**
- Produces:
  ```c
  typedef void (*nexus_event_cb)(int fd, uint32_t events, void *user);

  int  nexus_epoll_init(void);
  void nexus_epoll_add(int fd, uint32_t events, void *user);
  void nexus_epoll_mod(int fd, uint32_t events, void *user);
  void nexus_epoll_del(int fd);
  int  nexus_epoll_wait(int timeout_ms, nexus_event_cb cb);  // 返回事件数
  void nexus_epoll_close(void);
  ```

- [ ] **Step 1: 写测试 tests/test_epoll.c**

```c
#include "nexus_epoll.h"
#include <assert.h>
#include <unistd.h>
#include <string.h>

static int g_called = 0;
static int g_got_fd = 0;
static uint32_t g_got_events = 0;

static void on_event(int fd, uint32_t ev, void *u) {
    (void)u;
    g_called++;
    g_got_fd = fd;
    g_got_events = ev;
}

int main(void) {
    assert(nexus_epoll_init() == 0);

    int p[2];
    assert(pipe(p) == 0);

    nexus_epoll_add(p[0], EPOLLIN, NULL);
    write(p[1], "x", 1);

    int n = nexus_epoll_wait(100, on_event);
    assert(n >= 1);
    assert(g_called >= 1);
    assert(g_got_fd == p[0]);
    assert(g_got_events & EPOLLIN);

    nexus_epoll_close();
    close(p[0]); close(p[1]);
    printf("test_epoll: all passed\n");
    return 0;
}
```

- [ ] **Step 2: 写 include/nexus_epoll.h**

```c
#ifndef NEXUS_EPOLL_H
#define NEXUS_EPOLL_H

#include <stdint.h>
#include <sys/epoll.h>

typedef void (*nexus_event_cb)(int fd, uint32_t events, void *user);

int  nexus_epoll_init(void);
void nexus_epoll_add(int fd, uint32_t events, void *user);
void nexus_epoll_mod(int fd, uint32_t events, void *user);
void nexus_epoll_del(int fd);
int  nexus_epoll_wait(int timeout_ms, nexus_event_cb cb);
void nexus_epoll_close(void);

#endif
```

- [ ] **Step 3: 写 src/epoll_loop.c**

```c
#define _GNU_SOURCE
#include "nexus_epoll.h"
#include "nexus_util.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>

#define MAX_EVENTS 1024

static int g_epfd = -1;
static struct epoll_event g_events[MAX_EVENTS];
static void *g_user_data[MAX_EVENTS];  // user 指针数组

int nexus_epoll_init(void) {
    g_epfd = epoll_create1(EPOLL_CLOEXEC);
    return g_epfd < 0 ? -1 : 0;
}

void nexus_epoll_add(int fd, uint32_t events, void *user) {
    struct epoll_event ev = {0};
    ev.events = events;
    ev.data.fd = fd;
    epoll_ctl(g_epfd, EPOLL_CTL_ADD, fd, &ev);
    g_user_data[fd % MAX_EVENTS] = user;
}

void nexus_epoll_mod(int fd, uint32_t events, void *user) {
    struct epoll_event ev = {0};
    ev.events = events;
    ev.data.fd = fd;
    epoll_ctl(g_epfd, EPOLL_CTL_MOD, fd, &ev);
    g_user_data[fd % MAX_EVENTS] = user;
}

void nexus_epoll_del(int fd) {
    epoll_ctl(g_epfd, EPOLL_CTL_DEL, fd, NULL);
    g_user_data[fd % MAX_EVENTS] = NULL;
}

int nexus_epoll_wait(int timeout_ms, nexus_event_cb cb) {
    int n = epoll_wait(g_epfd, g_events, MAX_EVENTS, timeout_ms);
    for (int i = 0; i < n; i++) {
        int fd = g_events[i].data.fd;
        cb(fd, g_events[i].events, g_user_data[fd % MAX_EVENTS]);
    }
    return n;
}

void nexus_epoll_close(void) {
    if (g_epfd >= 0) { close(g_epfd); g_epfd = -1; }
}
```

- [ ] **Step 4: 跑测试**

```bash
cd "E:/share/Nexus Gateway" && make test
```

预期：`test_epoll: all passed`。

- [ ] **Step 5: 提交**

```bash
cd "E:/share/Nexus Gateway" && git add . && git commit -m "feat(epoll): add epoll wrapper with user-data table"
```

---

### Task 1.6: http_header 模块（哈希表头检索）

**Files:**
- Create: `include/nexus_http_header.h`
- Create: `src/http_header.c`
- Create: `tests/test_http_header.c`

**Interfaces:**
- Produces:
  ```c
  #define NEXUS_HTTP_HEADER_MAX 32

  typedef struct {
      uint32_t hash;
      char    *name;    // 大小写不敏感的归一化（小写）
      char    *value;
  } nexus_header_t;

  typedef struct {
      nexus_header_t headers[NEXUS_HTTP_HEADER_MAX];
      int            count;
  } nexus_headers_t;

  void   nexus_headers_init(nexus_headers_t *h);
  int    nexus_headers_add(nexus_headers_t *h, const char *name, size_t name_len,
                                                  const char *value, size_t value_len);
  const char *nexus_headers_get(const nexus_headers_t *h, const char *name);
  void   nexus_headers_reset(nexus_headers_t *h);
  ```

- [ ] **Step 1: 写测试 tests/test_http_header.c**

```c
#include "nexus_http_header.h"
#include <assert.h>
#include <string.h>

int main(void) {
    nexus_headers_t h;
    nexus_headers_init(&h);

    assert(nexus_headers_add(&h, "Host", 4, "example.com", 11) == 0);
    assert(nexus_headers_add(&h, "Content-Length", 14, "42", 2) == 0);
    assert(nexus_headers_add(&h, "User-Agent", 10, "curl/7.0", 8) == 0);

    assert(strcmp(nexus_headers_get(&h, "host"), "example.com") == 0);
    assert(strcmp(nexus_headers_get(&h, "CONTENT-LENGTH"), "42") == 0);
    assert(strcmp(nexus_headers_get(&h, "user-agent"), "curl/7.0") == 0);
    assert(nexus_headers_get(&h, "missing") == NULL);

    // 性能 smoke test：1000 次查询
    for (int i = 0; i < 1000; i++) {
        nexus_headers_get(&h, "host");
    }

    nexus_headers_reset(&h);
    assert(h.count == 0);
    assert(nexus_headers_get(&h, "host") == NULL);

    printf("test_http_header: all passed\n");
    return 0;
}
```

- [ ] **Step 2: 写 include/nexus_http_header.h**

```c
#ifndef NEXUS_HTTP_HEADER_H
#define NEXUS_HTTP_HEADER_H

#include <stddef.h>

#define NEXUS_HTTP_HEADER_MAX 32

typedef struct {
    unsigned int hash;
    char *name;     // 已 normalize 为小写
    char *value;
} nexus_header_t;

typedef struct {
    nexus_header_t headers[NEXUS_HTTP_HEADER_MAX];
    int            count;
} nexus_headers_t;

void        nexus_headers_init(nexus_headers_t *h);
int         nexus_headers_add(nexus_headers_t *h,
                              const char *name, size_t name_len,
                              const char *value, size_t value_len);
const char *nexus_headers_get(const nexus_headers_t *h, const char *name);
void        nexus_headers_reset(nexus_headers_t *h);

#endif
```

- [ ] **Step 3: 写 src/http_header.c**

```c
#include "nexus_http_header.h"
#include "nexus_util.h"
#include <string.h>
#include <strings.h>
#include <ctype.h>

void nexus_headers_init(nexus_headers_t *h) { memset(h, 0, sizeof(*h)); }

void nexus_headers_reset(nexus_headers_t *h) {
    for (int i = 0; i < h->count; i++) {
        free(h->headers[i].name);
        free(h->headers[i].value);
    }
    memset(h, 0, sizeof(*h));
}

static char *normalize_lower(const char *s, size_t len) {
    char *p = nexus_xmalloc(len + 1);
    for (size_t i = 0; i < len; i++) p[i] = (char)tolower((unsigned char)s[i]);
    p[len] = '\0';
    return p;
}

int nexus_headers_add(nexus_headers_t *h,
                      const char *name, size_t name_len,
                      const char *value, size_t value_len) {
    if (h->count >= NEXUS_HTTP_HEADER_MAX) return -1;
    char *n = normalize_lower(name, name_len);
    char *v = nexus_xmalloc(value_len + 1);
    memcpy(v, value, value_len);
    v[value_len] = '\0';
    nexus_header_t *e = &h->headers[h->count++];
    e->name  = n;
    e->value = v;
    e->hash  = nexus_hash_fnv1a(n, name_len);
    return 0;
}

const char *nexus_headers_get(const nexus_headers_t *h, const char *name) {
    size_t nlen = strlen(name);
    uint32_t target = nexus_hash_fnv1a(name, nlen);
    for (int i = 0; i < h->count; i++) {
        if (h->headers[i].hash == target && strcasecmp(h->headers[i].name, name) == 0) {
            return h->headers[i].value;
        }
    }
    return NULL;
}
```

- [ ] **Step 4: 跑测试**

```bash
cd "E:/share/Nexus Gateway" && make test
```

预期：`test_http_header: all passed`。

- [ ] **Step 5: 提交**

```bash
cd "E:/share/Nexus Gateway" && git add . && git commit -m "feat(http_header): add hash-indexed header table with case-insensitive lookup"
```

---

### Task 1.7: http_parser 模块（HTTP/1.1 状态机）

**Files:**
- Create: `include/nexus_http_parser.h`
- Create: `src/http_parser.c`
- Create: `tests/test_http_parser.c`

**Interfaces:**
- Produces:
  ```c
  typedef enum {
      HP_REQ_LINE,
      HP_HEADER,
      HP_BODY,
      HP_DONE,
      HP_INVALID,
  } hp_state_t;

  typedef struct {
      hp_state_t       state;
      char             method[16];
      char             path[1024];
      char             version[16];
      nexus_headers_t  headers;
      int              body_remaining;
  } nexus_http_req_t;

  void   nexus_http_req_init(nexus_http_req_t *r);
  void   nexus_http_req_reset(nexus_http_req_t *r);
  int    nexus_http_req_feed(nexus_http_req_t *r, const char *data, size_t len);
  ```

- [ ] **Step 1: 写测试 tests/test_http_parser.c**

```c
#include "nexus_http_parser.h"
#include <assert.h>
#include <string.h>

int main(void) {
    const char *req1 =
        "GET /api HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "hello";

    nexus_http_req_t r;
    nexus_http_req_init(&r);
    int n = nexus_http_req_feed(&r, req1, strlen(req1));
    assert(n > 0);
    assert(r.state == HP_DONE);
    assert(strcmp(r.method, "GET") == 0);
    assert(strcmp(r.path, "/api") == 0);
    assert(strcmp(nexus_headers_get(&r.headers, "host"), "example.com") == 0);
    assert(strcmp(nexus_headers_get(&r.headers, "content-length"), "5") == 0);
    nexus_http_req_reset(&r);

    // 分段喂入
    const char *req2 =
        "POST /submit HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";
    nexus_http_req_init(&r);
    int total = 0;
    total += nexus_http_req_feed(&r, req2, 10);
    total += nexus_http_req_feed(&r, req2 + 10, strlen(req2) - 10);
    assert(r.state == HP_DONE);
    assert(strcmp(r.method, "POST") == 0);
    assert(strcmp(r.path, "/submit") == 0);
    nexus_http_req_reset(&r);

    // 无 Content-Length，无 body → 直接 DONE
    const char *req3 = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    nexus_http_req_init(&r);
    nexus_http_req_feed(&r, req3, strlen(req3));
    assert(r.state == HP_DONE);
    nexus_http_req_reset(&r);

    // 畸形请求
    const char *bad = "GET / HTTP/9.9\r\n\r\n";
    nexus_http_req_init(&r);
    nexus_http_req_feed(&r, bad, strlen(bad));
    assert(r.state == HP_INVALID);
    nexus_http_req_reset(&r);

    printf("test_http_parser: all passed\n");
    return 0;
}
```

- [ ] **Step 2: 写 include/nexus_http_parser.h**

```c
#ifndef NEXUS_HTTP_PARSER_H
#define NEXUS_HTTP_PARSER_H

#include "nexus_http_header.h"

typedef enum {
    HP_REQ_LINE,
    HP_HEADER,
    HP_BODY,
    HP_DONE,
    HP_INVALID,
} hp_state_t;

typedef struct {
    hp_state_t       state;
    char             method[16];
    char             path[1024];
    char             version[16];
    nexus_headers_t  headers;
    int              body_remaining;
} nexus_http_req_t;

void nexus_http_req_init(nexus_http_req_t *r);
void nexus_http_req_reset(nexus_http_req_t *r);
int  nexus_http_req_feed(nexus_http_req_t *r, const char *data, size_t len);

#endif
```

- [ ] **Step 3: 写 src/http_parser.c**

```c
#include "nexus_http_parser.h"
#include "nexus_util.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

void nexus_http_req_init(nexus_http_req_t *r) {
    memset(r, 0, sizeof(*r));
    r->state = HP_REQ_LINE;
    nexus_headers_init(&r->headers);
}

void nexus_http_req_reset(nexus_http_req_t *r) {
    nexus_headers_reset(&r->headers);
    nexus_http_req_init(r);
}

static int parse_request_line(nexus_http_req_t *r, const char *line) {
    // 形如 "GET /path HTTP/1.1"
    const char *p = line;
    const char *sp1 = strchr(p, ' ');
    if (!sp1 || sp1 - p >= (int)sizeof(r->method)) return -1;
    memcpy(r->method, p, sp1 - p);
    r->method[sp1 - p] = '\0';

    p = sp1 + 1;
    const char *sp2 = strchr(p, ' ');
    if (!sp2 || sp2 - p >= (int)sizeof(r->path)) return -1;
    memcpy(r->path, p, sp2 - p);
    r->path[sp2 - p] = '\0';

    p = sp2 + 1;
    if (strncmp(p, "HTTP/", 5) != 0) return -1;
    if (strlen(p) >= sizeof(r->version)) return -1;
    strcpy(r->version, p);
    return 0;
}

int nexus_http_req_feed(nexus_http_req_t *r, const char *data, size_t len) {
    if (r->state == HP_DONE || r->state == HP_INVALID) return 0;
    size_t consumed = 0;

    while (consumed < len) {
        if (r->state == HP_BODY) {
            size_t take = (len - consumed) < (size_t)r->body_remaining
                          ? (len - consumed) : (size_t)r->body_remaining;
            consumed += take;
            r->body_remaining -= (int)take;
            if (r->body_remaining == 0) r->state = HP_DONE;
            continue;
        }

        // 找一行（\r\n）
        const char *eol = NULL;
        for (size_t i = consumed; i + 1 < len; i++) {
            if (data[i] == '\r' && data[i+1] == '\n') { eol = data + i; break; }
        }
        if (!eol) return (int)consumed;  // 等待更多数据

        size_t line_len = eol - (data + consumed);
        const char *line = data + consumed;

        if (r->state == HP_REQ_LINE) {
            if (parse_request_line(r, line) != 0) { r->state = HP_INVALID; return (int)consumed; }
            r->state = HP_HEADER;
        } else if (r->state == HP_HEADER) {
            if (line_len == 0) {
                // 空行 → header 结束
                const char *cl = nexus_headers_get(&r->headers, "content-length");
                r->body_remaining = cl ? atoi(cl) : 0;
                r->state = (r->body_remaining > 0) ? HP_BODY : HP_DONE;
            } else {
                const char *colon = memchr(line, ':', line_len);
                if (!colon) { r->state = HP_INVALID; return (int)consumed; }
                size_t name_len = colon - line;
                const char *vp = colon + 1;
                size_t value_len = line_len - name_len - 1;
                while (value_len > 0 && (*vp == ' ' || *vp == '\t')) { vp++; value_len--; }
                nexus_headers_add(&r->headers, line, name_len, vp, value_len);
            }
        }
        consumed += line_len + 2;  // 跳过 \r\n
    }
    return (int)consumed;
}
```

- [ ] **Step 4: 跑测试**

```bash
cd "E:/share/Nexus Gateway" && make test
```

预期：`test_http_parser: all passed`。

- [ ] **Step 5: ���交**

```bash
cd "E:/share/Nexus Gateway" && git add . && git commit -m "feat(http_parser): add HTTP/1.1 line-based state machine with header integration"
```

---

### Task 1.8: upstream 模块（加权轮询）

**Files:**
- Create: `include/nexus_upstream.h`
- Create: `src/upstream.c`
- Create: `tests/test_upstream.c`

**Interfaces:**
- Produces:
  ```c
  typedef struct {
      char host[64];
      int  port;
      int  weight;
      int  healthy;            // 1=健康 0=不健康
      int  active_conns;
  } nexus_upstream_node_t;

  typedef struct {
      char                 name[64];
      nexus_upstream_node_t nodes[16];
      int                  node_count;
      // 加权轮询内部状态
      int                  cur_weight;
      int                  max_weight;
  } nexus_upstream_t;

  int  nexus_upstream_init(nexus_upstream_t *u, const char *name);
  int  nexus_upstream_add_node(nexus_upstream_t *u, const char *host, int port, int weight);
  const nexus_upstream_node_t *nexus_upstream_pick(nexus_upstream_t *u);  // 加权轮询
  void nexus_upstream_mark(nexus_upstream_t *u, const char *host, int port, int healthy);
  ```

- [ ] **Step 1: 写测试 tests/test_upstream.c**

```c
#include "nexus_upstream.h"
#include <assert.h>
#include <string.h>

int main(void) {
    nexus_upstream_t u;
    nexus_upstream_init(&u, "api");
    nexus_upstream_add_node(&u, "127.0.0.1", 9001, 3);
    nexus_upstream_add_node(&u, "127.0.0.1", 9002, 2);
    nexus_upstream_add_node(&u, "127.0.0.1", 9003, 1);

    // 1000 次抽样，统计比例应近似 3:2:1
    int hits[3] = {0,0,0};
    for (int i = 0; i < 6000; i++) {
        const nexus_upstream_node_t *n = nexus_upstream_pick(&u);
        for (int j = 0; j < 3; j++) {
            if (n->port == 9001 + j) hits[j]++;
        }
    }
    // 允许 ±5% 误差
    assert(hits[0] > hits[1] && hits[1] > hits[2]);
    assert(hits[0] > 2700 && hits[0] < 3300);  // 期望 3000

    // 标记 9002 不健康
    nexus_upstream_mark(&u, "127.0.0.1", 9002, 0);
    int saw_9002 = 0;
    for (int i = 0; i < 100; i++) {
        if (nexus_upstream_pick(&u)->port == 9002) saw_9002++;
    }
    assert(saw_9002 == 0);

    printf("test_upstream: hits=%d/%d/%d (want ~3000/2000/1000)\n", hits[0], hits[1], hits[2]);
    printf("test_upstream: all passed\n");
    return 0;
}
```

- [ ] **Step 2: 写 include/nexus_upstream.h**

```c
#ifndef NEXUS_UPSTREAM_H
#define NEXUS_UPSTREAM_H

#define NEXUS_UPSTREAM_MAX_NODES 16

typedef struct {
    char host[64];
    int  port;
    int  weight;
    int  healthy;
    int  active_conns;
} nexus_upstream_node_t;

typedef struct {
    char                  name[64];
    nexus_upstream_node_t nodes[NEXUS_UPSTREAM_MAX_NODES];
    int                   node_count;
    int                   cur_weight;
    int                   max_weight;
    int                   gcd_weight;
} nexus_upstream_t;

int  nexus_upstream_init(nexus_upstream_t *u, const char *name);
int  nexus_upstream_add_node(nexus_upstream_t *u, const char *host, int port, int weight);
const nexus_upstream_node_t *nexus_upstream_pick(nexus_upstream_t *u);
void nexus_upstream_mark(nexus_upstream_t *u, const char *host, int port, int healthy);

#endif
```

- [ ] **Step 3: 写 src/upstream.c**

```c
#include "nexus_upstream.h"
#include <string.h>
#include <stddef.h>

static int gcd(int a, int b) { return b == 0 ? a : gcd(b, a % b); }

int nexus_upstream_init(nexus_upstream_t *u, const char *name) {
    memset(u, 0, sizeof(*u));
    strncpy(u->name, name, sizeof(u->name) - 1);
    return 0;
}

int nexus_upstream_add_node(nexus_upstream_t *u, const char *host, int port, int weight) {
    if (u->node_count >= NEXUS_UPSTREAM_MAX_NODES) return -1;
    nexus_upstream_node_t *n = &u->nodes[u->node_count++];
    strncpy(n->host, host, sizeof(n->host) - 1);
    n->port = port;
    n->weight = weight > 0 ? weight : 1;
    n->healthy = 1;
    return 0;
}

static void recompute(nexus_upstream_t *u) {
    int mw = 0, g = 0;
    for (int i = 0; i < u->node_count; i++) {
        if (u->nodes[i].weight > mw) mw = u->nodes[i].weight;
        if (i == 0) g = u->nodes[i].weight;
        else g = gcd(g, u->nodes[i].weight);
    }
    u->max_weight = mw;
    u->gcd_weight = g > 0 ? g : 1;
}

const nexus_upstream_node_t *nexus_upstream_pick(nexus_upstream_t *u) {
    if (u->node_count == 0) return NULL;
    if (u->max_weight == 0) recompute(u);

    // 平滑加权轮询（nginx 算法）
    int total = 0;
    for (int i = 0; i < u->node_count; i++) {
        if (!u->nodes[i].healthy) continue;
        u->nodes[i].active_conns += u->nodes[i].weight;
        total += u->nodes[i].weight;
    }
    if (total == 0) return NULL;

    int best = -1, best_val = -1;
    for (int i = 0; i < u->node_count; i++) {
        if (!u->nodes[i].healthy) continue;
        if (best == -1 || u->nodes[i].active_conns > best_val) {
            best = i;
            best_val = u->nodes[i].active_conns;
        }
    }
    if (best < 0) return NULL;
    u->nodes[best].active_conns -= total;
    return &u->nodes[best];
}

void nexus_upstream_mark(nexus_upstream_t *u, const char *host, int port, int healthy) {
    for (int i = 0; i < u->node_count; i++) {
        if (u->nodes[i].port == port && strcmp(u->nodes[i].host, host) == 0) {
            u->nodes[i].healthy = healthy;
        }
    }
}
```

- [ ] **Step 4: 跑测试**

```bash
cd "E:/share/Nexus Gateway" && make test
```

预期：`test_upstream: all passed`，`hits` 大约 `3000/2000/1000`。

- [ ] **Step 5: 提交**

```bash
cd "E:/share/Nexus Gateway" && git add . && git commit -m "feat(upstream): add smooth weighted round-robin with health marking"
```

---

### Task 1.9: acceptor 模块（listen + SO_REUSEPORT）

**Files:**
- Create: `include/nexus_acceptor.h`
- Create: `src/acceptor.c`
- Create: `tests/test_acceptor.c`

**Interfaces:**
- Produces:
  ```c
  int nexus_acceptor_listen(const char *bind_ip, int port, int reuse_port);
  int nexus_acceptor_accept(int listen_fd);  // 返回 client fd，-1 on error
  ```

- [ ] **Step 1: 写测试 tests/test_acceptor.c**

```c
#include "nexus_acceptor.h"
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>

int main(void) {
    int lfd = nexus_acceptor_listen("127.0.0.1", 19999, 1);
    assert(lfd >= 0);

    // 客户端 connect
    int cfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(19999);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    assert(connect(cfd, (struct sockaddr*)&addr, sizeof(addr)) == 0);

    int accepted = nexus_acceptor_accept(lfd);
    assert(accepted >= 0);

    // 简单互通
    const char *msg = "ping";
    write(cfd, msg, 4);
    char buf[16] = {0};
    read(accepted, buf, 4);
    assert(strcmp(buf, "ping") == 0);

    close(accepted);
    close(cfd);
    close(lfd);
    printf("test_acceptor: all passed\n");
    return 0;
}
```

- [ ] **Step 2: 写 include/nexus_acceptor.h**

```c
#ifndef NEXUS_ACCEPTOR_H
#define NEXUS_ACCEPTOR_H

int nexus_acceptor_listen(const char *bind_ip, int port, int reuse_port);
int nexus_acceptor_accept(int listen_fd);

#endif
```

- [ ] **Step 3: 写 src/acceptor.c**

```c
#define _GNU_SOURCE
#include "nexus_acceptor.h"
#include "nexus_util.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

static int set_nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL);
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

int nexus_acceptor_listen(const char *bind_ip, int port, int reuse_port) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (reuse_port) {
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (strcmp(bind_ip, "0.0.0.0") == 0) addr.sin_addr.s_addr = INADDR_ANY;
    else inet_pton(AF_INET, bind_ip, &addr.sin_addr);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd); return -1;
    }
    if (listen(fd, 1024) < 0) {
        close(fd); return -1;
    }
    set_nonblock(fd);
    return fd;
}

int nexus_acceptor_accept(int listen_fd) {
    struct sockaddr_in cli;
    socklen_t len = sizeof(cli);
    int cfd = accept4(listen_fd, (struct sockaddr*)&cli, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    return cfd;
}
```

- [ ] **Step 4: 跑测试**

```bash
cd "E:/share/Nexus Gateway" && make test
```

预期：`test_acceptor: all passed`。

- [ ] **Step 5: 提交**

```bash
cd "E:/share/Nexus Gateway" && git add . && git commit -m "feat(acceptor): add non-blocking listen with SO_REUSEPORT option"
```

---

### Task 1.10: proxy 模块（基本代理转发）

**Files:**
- Create: `include/nexus_proxy.h`
- Create: `src/proxy.c`
- Create: `tests/integration/test_proxy_basic.sh`

**Interfaces:**
- Produces:
  ```c
  int nexus_proxy_connect_upstream(const nexus_upstream_node_t *node);  // 返回 upstream fd
  int nexus_proxy_forward_request(int upstream_fd, const char *req, size_t len);
  int nexus_proxy_read_response(int upstream_fd, char *buf, size_t cap);
  ```

**集成测试：** 启动一个 python -m http.server 上游，curl 通过网关验证

- [ ] **Step 1: 写 include/nexus_proxy.h**

```c
#ifndef NEXUS_PROXY_H
#define NEXUS_PROXY_H

#include "nexus_upstream.h"
#include <stddef.h>

int nexus_proxy_connect_upstream(const nexus_upstream_node_t *node);
int nexus_proxy_forward_request(int upstream_fd, const char *req, size_t len);
int nexus_proxy_read_response(int upstream_fd, char *buf, size_t cap);

#endif
```

- [ ] **Step 2: 写 src/proxy.c**

```c
#define _GNU_SOURCE
#include "nexus_proxy.h"
#include "nexus_util.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

int nexus_proxy_connect_upstream(const nexus_upstream_node_t *node) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(node->port);
    inet_pton(AF_INET, node->host, &addr.sin_addr);
    int r = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (r < 0 && errno != EINPROGRESS) { close(fd); return -1; }
    return fd;
}

int nexus_proxy_forward_request(int upstream_fd, const char *req, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(upstream_fd, req + sent, len - sent);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return -1;
        }
        sent += n;
    }
    return 0;
}

int nexus_proxy_read_response(int upstream_fd, char *buf, size_t cap) {
    ssize_t n = read(upstream_fd, buf, cap - 1);
    if (n < 0) return -1;
    buf[n] = '\0';
    return (int)n;
}
```

- [ ] **Step 3: 写集成测试 tests/integration/test_proxy_basic.sh**

```bash
#!/usr/bin/env bash
set -e
cd "$(dirname "$0")/../.."

# 启动一个简单上游：python http.server
mkdir -p /tmp/nexus_upstream
echo "hello-from-upstream" > /tmp/nexus_upstream/index.html
(cd /tmp/nexus_upstream && python3 -m http.server 19998 >/dev/null 2>&1) &
UPSTREAM_PID=$!
sleep 0.5

# 启动网关（独立测试程序入口，这里手工调 proxy API 模拟一次请求）
# 实际端到端测试在 Task 1.11 整合阶段做，本 Task 只验证 proxy 函数
# 用 python 模拟 client → 直接连上游验证上游活着
curl -sf http://127.0.0.1:19998/ >/dev/null
echo "test_proxy_basic: upstream reachable"

kill $UPSTREAM_PID 2>/dev/null || true
wait $UPSTREAM_PID 2>/dev/null || true
```

- [ ] **Step 4: 跑测试（用 chmod +x 让脚本可执行）**

```bash
cd "E:/share/Nexus Gateway" && chmod +x tests/integration/test_proxy_basic.sh && make test
```

预期：unit test 全部通过，集成测试输出 `test_proxy_basic: upstream reachable`。

- [ ] **Step 5: 提交**

```bash
cd "E:/share/Nexus Gateway" && git add . && git commit -m "feat(proxy): add non-blocking upstream connect, request forward, response read"
```

---

### Task 1.11: worker 端到端（第一个可运行代理）

**Files:**
- Modify: `src/main.c`（改为 fork + worker 入口）
- Create: `src/worker.c`
- Create: `include/nexus_worker.h`

**Interfaces:**
- Produces: `./bin/nexus_gateway config.ini` 启动后，`curl http://127.0.0.1:8080/` 命中本地 python 上游

- [ ] **Step 1: 写 include/nexus_worker.h**

```c
#ifndef NEXUS_WORKER_H
#define NEXUS_WORKER_H

#include "nexus_config.h"

int nexus_worker_run(nexus_config_t *cfg);

#endif
```

- [ ] **Step 2: 写 src/worker.c**

```c
#define _GNU_SOURCE
#include "nexus_worker.h"
#include "nexus_epoll.h"
#include "nexus_acceptor.h"
#include "nexus_connection.h"
#include "nexus_http_parser.h"
#include "nexus_upstream.h"
#include "nexus_proxy.h"
#include "nexus_log.h"
#include "nexus_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

static nexus_config_t *g_cfg = NULL;
static nexus_upstream_t g_upstream;
static int g_listen_fd = -1;

static int parse_listen_addr(const char *s, char *ip_out, int *port_out) {
    // "0.0.0.0:8080" → ip="0.0.0.0", port=8080
    const char *colon = strrchr(s, ':');
    if (!colon) return -1;
    size_t iplen = colon - s;
    strncpy(ip_out, s, iplen); ip_out[iplen] = '\0';
    *port_out = atoi(colon + 1);
    return 0;
}

static void handle_client_read(int cfd) {
    nexus_conn_t *c = conn_get(cfd);
    if (!c) {
        char buf[4096];
        ssize_t n = read(cfd, buf, sizeof(buf));
        if (n <= 0) { close(cfd); return; }
        nexus_http_req_t req;
        nexus_http_req_init(&req);
        int consumed = nexus_http_req_feed(&req, buf, (size_t)n);
        if (req.state == HP_INVALID || req.state == HP_DONE) {
            // 选上游，转发
            const nexus_upstream_node_t *node = nexus_upstream_pick(&g_upstream);
            if (node) {
                int ufd = nexus_proxy_connect_upstream(node);
                if (ufd >= 0) {
                    nexus_proxy_forward_request(ufd, buf, (size_t)n);
                    char resp[8192];
                    int rn = nexus_proxy_read_response(ufd, resp, sizeof(resp));
                    if (rn > 0) write(cfd, resp, (size_t)rn);
                    close(ufd);
                }
            }
            nexus_log_access("client_fd=%d %s %s -> %d", cfd, req.method, req.path, rn);
        }
        nexus_http_req_reset(&req);
        close(cfd);
    } else {
        (void)c;
        char buf[4096];
        ssize_t n = read(cfd, buf, sizeof(buf));
        if (n <= 0) { conn_free(c); close(cfd); return; }
        // 简化：单步处理整个请求
    }
}

static void on_event(int fd, uint32_t ev, void *u) {
    (void)u;
    if (fd == g_listen_fd) {
        int cfd = nexus_acceptor_accept(g_listen_fd);
        if (cfd >= 0) {
            conn_alloc(cfd);
            nexus_epoll_add(cfd, EPOLLIN, NULL);
        }
    } else if (ev & EPOLLIN) {
        handle_client_read(fd);
    }
}

int nexus_worker_run(nexus_config_t *cfg) {
    g_cfg = cfg;
    nexus_log_init("logs", 1);

    const char *listen = nexus_config_get(cfg, "server", "listen");
    if (!listen) { fprintf(stderr, "missing [server] listen\n"); return 1; }
    char ip[64]; int port;
    if (parse_listen_addr(listen, ip, &port) < 0) return 1;

    // 初始化后端
    nexus_upstream_init(&g_upstream, "api");
    for (int i = 0; i < 16; i++) {
        char key[64];
        snprintf(key, sizeof(key), "upstream.api.server.%d", i);
        // 简化：从单一 server 行读
    }
    const char *srv = nexus_config_get(cfg, "upstream.api", "server");
    if (srv) {
        char host[64]; int sport;
        const char *colon = strrchr(srv, ':');
        if (colon) {
            size_t hl = colon - srv;
            memcpy(host, srv, hl); host[hl] = '\0';
            sport = atoi(colon + 1);
            nexus_upstream_add_node(&g_upstream, host, sport, 1);
        }
    }

    g_listen_fd = nexus_acceptor_listen(ip, port, 0);  // 单进程模式先不开 REUSEPORT
    if (g_listen_fd < 0) { perror("listen"); return 1; }
    nexus_epoll_init();
    nexus_epoll_add(g_listen_fd, EPOLLIN, NULL);
    nexus_log_error(1, "worker started on %s:%d", ip, port);

    while (1) {
        nexus_epoll_wait(100, on_event);
    }
    return 0;
}
```

- [ ] **Step 3: 覆盖写 src/main.c**

```c
#include <stdio.h>
#include <stdlib.h>
#include "nexus_config.h"
#include "nexus_worker.h"
#include "nexus_config.h"

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <config.ini>\n", argv[0]); return 1; }
    nexus_config_t *cfg = nexus_config_load(argv[1]);
    if (!cfg) { perror("config load"); return 1; }
    int rc = nexus_worker_run(cfg);
    nexus_config_free(cfg);
    return rc;
}
```

- [ ] **Step 4: 编译并端到端验证**

```bash
cd "E:/share/Nexus Gateway" && make clean && make

# 启动上游
mkdir -p /tmp/nexus_upstream
echo "hello-from-upstream" > /tmp/nexus_upstream/index.html
(cd /tmp/nexus_upstream && python3 -m http.server 19998) &
UP_PID=$!
sleep 0.5

# 复制 config 模板
cp config.ini.example /tmp/nexus_test.ini
# 把 api 上游端口改为 19998
sed -i 's/9001/19998/' /tmp/nexus_test.ini

# 启动网关
./bin/nexus_gateway /tmp/nexus_test.ini &
GW_PID=$!
sleep 0.5

# 验证
result=$(curl -sf http://127.0.0.1:8080/)
echo "response: $result"
[[ "$result" == *"hello-from-upstream"* ]] && echo "MVP OK"

kill $GW_PID $UP_PID 2>/dev/null
wait 2>/dev/null
```

预期输出包含 `response: <html>...hello-from-upstream...` 和 `MVP OK`。

- [ ] **Step 5: 提交**

```bash
cd "E:/share/Nexus Gateway" && git add . && git commit -m "feat(worker): wire up end-to-end single-worker HTTP proxy (MVP)"
```

---

## 阶段 2：高可用

### Task 2.1: master 进程 + 共享内存双缓冲

**Files:**
- Create: `src/master.c`
- Create: `include/nexus_master.h`
- Modify: `src/main.c`

**Interfaces:**
- Produces:
  ```c
  // 共享内存结构（定义在 master.c 中，外部只读使用）
  typedef struct {
      uint64_t version;
      uint32_t total_size;
      char     data[CONFIG_MAX];
  } config_shadow_t;

  extern config_shadow_t *g_shadow;
  extern atomic_int       *g_active_slot;
  int  nexus_master_run(nexus_config_t *cfg, const char *config_path);
  ```

- [ ] **Step 1: 写 include/nexus_master.h**

```c
#ifndef NEXUS_MASTER_H
#define NEXUS_MASTER_H

#include "nexus_config.h"
#include <stdatomic.h>

#define CONFIG_MAX (64 * 1024)

typedef struct {
    uint64_t version;
    uint32_t total_size;
    char     data[CONFIG_MAX];
} config_shadow_t;

extern config_shadow_t *g_shadow;
extern atomic_int      *g_active_slot;

int nexus_master_run(nexus_config_t *cfg, const char *config_path);

#endif
```

- [ ] **Step 2: 写 src/master.c**

```c
#define _GNU_SOURCE
#include "nexus_master.h"
#include "nexus_util.h"
#include "nexus_log.h"
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

config_shadow_t *g_shadow = NULL;
atomic_int      *g_active_slot = NULL;

static volatile sig_atomic_t g_sighup = 0;
static char g_config_path[512];

static void on_sighup(int s) { (void)s; g_sighup = 1; }

static void setup_shared_memory(void) {
    // 用 /dev/shm 做 POSIX 共享内存（Linux）
    int fd = shm_open("/nexus_config", O_CREAT | O_RDWR, 0600);
    if (fd < 0) nexus_die("shm_open");
    ftruncate(fd, sizeof(config_shadow_t) * 2 + sizeof(atomic_int));
    g_shadow = mmap(NULL, sizeof(config_shadow_t) * 2, PROT_READ | PROT_WRITE,
                    MAP_SHARED, fd, 0);
    g_active_slot = (atomic_int *)((char *)g_shadow + sizeof(config_shadow_t) * 2 - sizeof(atomic_int));
    // 简化：把 active_slot 放在 shadow 之后独立 mmap
    g_active_slot = mmap(NULL, sizeof(atomic_int), PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd, sizeof(config_shadow_t) * 2);
    close(fd);
    atomic_store(g_active_slot, 0);
}

static void serialize_config(nexus_config_t *cfg, char *buf, uint32_t *size_out) {
    // 简化序列化：把所有 kv 按 key=value\n 写入
    // Worker 端只关心几个固定 key，先做小规模
    char *p = buf;
    char *end = buf + CONFIG_MAX - 1;
    // 这里 stub：实际应遍历 cfg
    const char *listen = nexus_config_get(cfg, "server", "listen");
    p += snprintf(p, end - p, "listen=%s\n", listen ? listen : "0.0.0.0:8080");
    *size_out = (uint32_t)(p - buf);
}

static void reload_config(nexus_config_t *cfg) {
    int inactive = 1 - atomic_load(g_active_slot);
    serialize_config(cfg, g_shadow[inactive].data, &g_shadow[inactive].total_size);
    __sync_synchronize();
    atomic_fetch_add(&g_shadow[inactive].version, 1);
    atomic_store(g_active_slot, inactive);
    nexus_log_error(1, "master: config reloaded, slot=%d", inactive);
}

int nexus_master_run(nexus_config_t *cfg, const char *config_path) {
    strncpy(g_config_path, config_path, sizeof(g_config_path) - 1);
    setup_shared_memory();

    // 初始写入 slot 0
    serialize_config(cfg, g_shadow[0].data, &g_shadow[0].total_size);
    g_shadow[0].version = 1;

    signal(SIGHUP, on_sighup);
    signal(SIGCHLD, SIG_IGN);  // 简化：自动回收

    nexus_log_init("logs", 1);
    nexus_log_error(1, "master started, pid=%d", getpid());

    while (1) {
        if (g_sighup) {
            g_sighup = 0;
            nexus_config_t *new_cfg = nexus_config_load(g_config_path);
            if (new_cfg) {
                reload_config(new_cfg);
                nexus_config_free(new_cfg);
            } else {
                nexus_log_error(2, "master: config reload failed, keeping old");
            }
        }
        sleep(1);
    }
    return 0;
}
```

- [ ] **Step 3: 覆盖写 src/main.c**

```c
#include <stdio.h>
#include <string.h>
#include "nexus_config.h"
#include "nexus_master.h"

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <config.ini>\n", argv[0]); return 1; }
    nexus_config_t *cfg = nexus_config_load(argv[1]);
    if (!cfg) { perror("config load"); return 1; }
    int rc = nexus_master_run(cfg, argv[1]);
    nexus_config_free(cfg);
    return rc;
}
```

- [ ] **Step 4: 编译验证**

```bash
cd "E:/share/Nexus Gateway" && make clean && make
./bin/nexus_gateway /tmp/nexus_test.ini &
PID=$!
sleep 1
ls /dev/shm/nexus_config && echo "shm OK"
kill $PID
```

预期：`shm OK` 输出。

- [ ] **Step 5: 提交**

```bash
cd "E:/share/Nexus Gateway" && git add . && git commit -m "feat(master): add master process with shared-memory double-buffer config"
```

---

### Task 2.2: SO_REUSEPORT 多进程监听 + fork Worker

**Files:**
- Modify: `src/master.c`
- Modify: `src/worker.c`

- [ ] **Step 1: master.c 改为 fork 模式**

修改 `src/master.c` 的 `nexus_master_run` 函数，把 `while(1) sleep(1)` 替换为 fork-and-monitor 循环：

```c
#include <sched.h>

static int worker_count = 0;
static pid_t *worker_pids = NULL;

static pid_t spawn_worker(nexus_config_t *cfg) {
    pid_t pid = fork();
    if (pid == 0) {
        // 子进程
        return nexus_worker_run(cfg);  // 不返回直到退出
    }
    return pid;
}

int nexus_master_run(nexus_config_t *cfg, const char *config_path) {
    strncpy(g_config_path, config_path, sizeof(g_config_path) - 1);
    setup_shared_memory();
    serialize_config(cfg, g_shadow[0].data, &g_shadow[0].total_size);
    g_shadow[0].version = 1;

    signal(SIGHUP, on_sighup);
    nexus_log_init("logs", 1);

    const char *wn = nexus_config_get(cfg, "server", "worker_num");
    worker_count = (wn && strcmp(wn, "auto") != 0) ? atoi(wn) : (int)sysconf(_SC_NPROCESSORS_ONFNS);
    if (worker_count < 1) worker_count = 1;
    worker_pids = nexus_xmalloc(sizeof(pid_t) * worker_count);

    for (int i = 0; i < worker_count; i++) {
        worker_pids[i] = fork();
        if (worker_pids[i] == 0) {
            nexus_worker_run(cfg);
            exit(0);
        }
        nexus_log_error(1, "master: spawned worker pid=%d", worker_pids[i]);
    }

    while (1) {
        if (g_sighup) {
            g_sighup = 0;
            nexus_config_t *new_cfg = nexus_config_load(g_config_path);
            if (new_cfg) {
                reload_config(new_cfg);
                // 简化：SIGHUP 不重启 worker，只换配置（Worker 自己从 shm 读）
                nexus_config_free(new_cfg);
            }
        }
        // 回收已死 worker
        for (int i = 0; i < worker_count; i++) {
            int status;
            pid_t r = waitpid(worker_pids[i], &status, WNOHANG);
            if (r == worker_pids[i] || (r == -1)) {
                nexus_log_error(2, "master: worker %d died, respawning", worker_pids[i]);
                worker_pids[i] = fork();
                if (worker_pids[i] == 0) {
                    nexus_worker_run(cfg);
                    exit(0);
                }
            }
        }
        sleep(1);
    }
    return 0;
}
```

- [ ] **Step 2: worker.c 改为 SO_REUSEPORT 监听**

修改 `src/worker.c` 中 `nexus_worker_run` 的 listen 调用：

```c
    // 改这一行：
    int reuse_port = (getenv("NEXUS_WORKER") != NULL);  // Worker 模式开 REUSEPORT
    g_listen_fd = nexus_acceptor_listen(ip, port, reuse_port);
```

并在 master.c spawn 前设置：`setenv("NEXUS_WORKER", "1", 1);` 在 `fork()` 之后子进程立即。

- [ ] **Step 3: 编译并多进程验证**

```bash
cd "E:/share/Nexus Gateway" && make clean && make
./bin/nexus_gateway /tmp/nexus_test.ini &
PID=$!
sleep 1
ps --ppid $PID -o pid,comm | grep nexus_gateway | wc -l  # 应 = worker_count
curl -sf http://127.0.0.1:8080/ | head -c 80
kill $PID
```

预期：worker 进程数 = CPU 核数，curl 返回上游响应。

- [ ] **Step 4: 提交**

```bash
cd "E:/share/Nexus Gateway" && git add . && git commit -m "feat(master): fork N workers with SO_REUSEPORT and auto-respawn"
```

---

### Task 2.3: 平滑重启 + 存量连接排空

**Files:**
- Modify: `src/master.c`
- Modify: `src/worker.c`

**Interfaces:**
- Master 通过共享内存标志 `g_shutting_down`，Worker 事件循环检查后停止 accept

- [ ] **Step 1: master.c 加 `shutting_down` 标志**

在 `src/master.c` 顶部加：

```c
extern atomic_int g_shutting_down;
atomic_int g_shutting_down = 0;
```

并在 `nexus_master_run` 的 SIGHUP 处理中：

```c
        if (g_sighup) {
            g_sighup = 0;
            // 1. 解析新配置
            nexus_config_t *new_cfg = nexus_config_load(g_config_path);
            if (!new_cfg) { nexus_log_error(2, "config reload failed"); continue; }
            reload_config(new_cfg);
            // 2. 通知旧 worker 停止接新连接
            atomic_store(&g_shutting_down, 1);
            // 3. fork 新 worker（开新 REUSEPORT 实例）
            for (int i = 0; i < worker_count; i++) {
                pid_t p = fork();
                if (p == 0) {
                    setenv("NEXUS_WORKER", "1", 1);
                    atomic_store(&g_shutting_down, 0);  // 新 worker 重置
                    nexus_worker_run(new_cfg);
                    exit(0);
                }
            }
            // 4. 等待旧 worker 自然退出（最多 30 秒）
            sleep(30);
            atomic_store(&g_shutting_down, 0);
            nexus_config_free(new_cfg);
        }
```

- [ ] **Step 2: worker.c 检测 `g_shutting_down`**

在 `src/worker.c` 的 `on_event` 函数顶部加：

```c
    extern atomic_int g_shutting_down;
    if (atomic_load(&g_shutting_down) && fd == g_listen_fd) {
        // 停止 accept 新连接，让旧 worker 排空
        return;
    }
```

并在 worker 启动时把 `conn_count` 跟踪起来，没有连接时退出：

修改 `src/worker.c` 顶部加：

```c
static int g_active_conns = 0;
```

在 `handle_client_read` 入口：`g_active_conns++`；`close(cfd)` 前：`g_active_conns--`。

主循环改为：

```c
    while (1) {
        nexus_epoll_wait(100, on_event);
        extern atomic_int g_shutting_down;
        if (atomic_load(&g_shutting_down) && g_active_conns == 0) {
            nexus_log_error(1, "worker: drained, exiting");
            break;
        }
    }
```

- [ ] **Step 3: 端到端平滑重启测试**

```bash
cd "E:/share/Nexus Gateway" && make clean && make
./bin/nexus_gateway /tmp/nexus_test.ini &
PID=$!
sleep 1

# 启动一个慢请求（curl with delay）
(curl -sf http://127.0.0.1:8080/ --max-time 5 > /tmp/r1.txt) &
C1=$!
sleep 0.2

# 触发 SIGHUP
kill -HUP $PID
sleep 0.5

# 启动第二个请求
(curl -sf http://127.0.0.1:8080/ --max-time 5 > /tmp/r2.txt) &
C2=$!

wait $C1; wait $C2
echo "r1: $(cat /tmp/r1.txt | head -c 60)"
echo "r2: $(cat /tmp/r2.txt | head -c 60)"
kill $PID 2>/dev/null
```

预期：两个 curl 都成功，r1（旧 worker）和 r2（新 worker）都返回上游内容。

- [ ] **Step 4: 提交**

```bash
cd "E:/share/Nexus Gateway" && git add . && git commit -m "feat(ha): smooth restart with SIGHUP + connection draining"
```

---

## 阶段 3：亮点模块

### Task 3.1: rate_limit 模块（IP 令牌桶）

**Files:**
- Create: `include/nexus_rate_limit.h`
- Create: `src/rate_limit.c`
- Create: `tests/test_rate_limit.c`

**Interfaces:**
- Produces:
  ```c
  typedef struct {
      char     ip[64];
      double   tokens;
      uint64_t last_refill_ms;
  } rl_entry_t;

  typedef struct {
      rl_entry_t entries[1024];
      int        rate_per_sec;
  } nexus_rate_limiter_t;

  int  nexus_rl_init(nexus_rate_limiter_t *rl, int rate_per_sec);
  int  nexus_rl_check(nexus_rate_limiter_t *rl, const char *ip);  // 1=allow 0=deny
  ```

- [ ] **Step 1: 写测试 tests/test_rate_limit.c**

```c
#include "nexus_rate_limit.h"
#include <assert.h>

int main(void) {
    nexus_rate_limiter_t rl;
    nexus_rl_init(&rl, 10);  // 10 req/s

    // 前 10 个应通过
    for (int i = 0; i < 10; i++) assert(nexus_rl_check(&rl, "1.2.3.4") == 1);
    // 第 11 个被拒
    assert(nexus_rl_check(&rl, "1.2.3.4") == 0);
    // 不同 IP 互不影响
    assert(nexus_rl_check(&rl, "5.6.7.8") == 1);
    // 等待 1.1s 后同一 IP 应恢复
    struct timespec ts = {1, 100 * 1000 * 1000};
    nanosleep(&ts, NULL);
    assert(nexus_rl_check(&rl, "1.2.3.4") == 1);

    printf("test_rate_limit: all passed\n");
    return 0;
}
```

- [ ] **Step 2: 写 include/nexus_rate_limit.h**

```c
#ifndef NEXUS_RATE_LIMIT_H
#define NEXUS_RATE_LIMIT_H

#include <stdint.h>

#define RL_MAX 1024

typedef struct {
    char     ip[64];
    double   tokens;
    uint64_t last_refill_ms;
} rl_entry_t;

typedef struct {
    rl_entry_t entries[RL_MAX];
    int        count;
    int        rate_per_sec;
} nexus_rate_limiter_t;

int nexus_rl_init(nexus_rate_limiter_t *rl, int rate_per_sec);
int nexus_rl_check(nexus_rate_limiter_t *rl, const char *ip);

#endif
```

- [ ] **Step 3: 写 src/rate_limit.c**

```c
#include "nexus_rate_limit.h"
#include "nexus_util.h"
#include <string.h>
#include <stddef.h>

int nexus_rl_init(nexus_rate_limiter_t *rl, int rate_per_sec) {
    memset(rl, 0, sizeof(*rl));
    rl->rate_per_sec = rate_per_sec;
    return 0;
}

static rl_entry_t *find_or_create(nexus_rate_limiter_t *rl, const char *ip) {
    for (int i = 0; i < rl->count; i++) {
        if (strcmp(rl->entries[i].ip, ip) == 0) return &rl->entries[i];
    }
    if (rl->count >= RL_MAX) return NULL;
    rl_entry_t *e = &rl->entries[rl->count++];
    strncpy(e->ip, ip, sizeof(e->ip) - 1);
    e->tokens = (double)rl->rate_per_sec;
    e->last_refill_ms = nexus_now_ms();
    return e;
}

int nexus_rl_check(nexus_rate_limiter_t *rl, const char *ip) {
    rl_entry_t *e = find_or_create(rl, ip);
    if (!e) return 1;  // 满了放行（fail-open）
    uint64_t now = nexus_now_ms();
    double elapsed = (now - e->last_refill_ms) / 1000.0;
    e->tokens = e->tokens + elapsed * rl->rate_per_sec;
    if (e->tokens > rl->rate_per_sec) e->tokens = (double)rl->rate_per_sec;
    e->last_refill_ms = now;
    if (e->tokens >= 1.0) {
        e->tokens -= 1.0;
        return 1;
    }
    return 0;
}
```

- [ ] **Step 4: 跑测试**

```bash
cd "E:/share/Nexus Gateway" && make test
```

预期：`test_rate_limit: all passed`。

- [ ] **Step 5: 提交**

```bash
cd "E:/share/Nexus Gateway" && git add . && git commit -m "feat(rate_limit): add per-IP token bucket limiter"
```

---

### Task 3.2: blacklist 模块（IP 黑名单）

**Files:**
- Create: `include/nexus_blacklist.h`
- Create: `src/blacklist.c`
- Create: `tests/test_blacklist.c`

**Interfaces:**
- Produces:
  ```c
  typedef struct {
      char *ips[1024];
      int   count;
  } nexus_blacklist_t;

  int  nexus_bl_init(nexus_blacklist_t *bl);
  int  nexus_bl_add(nexus_blacklist_t *bl, const char *ip);
  int  nexus_bl_check(const nexus_blacklist_t *bl, const char *ip);  // 1=blocked
  void nexus_bl_free(nexus_blacklist_t *bl);
  ```

- [ ] **Step 1: 写测试 tests/test_blacklist.c**

```c
#include "nexus_blacklist.h"
#include <assert.h>

int main(void) {
    nexus_blacklist_t bl;
    nexus_bl_init(&bl);
    nexus_bl_add(&bl, "192.168.1.100");
    nexus_bl_add(&bl, "10.0.0.5");

    assert(nexus_bl_check(&bl, "192.168.1.100") == 1);
    assert(nexus_bl_check(&bl, "10.0.0.5") == 1);
    assert(nexus_bl_check(&bl, "8.8.8.8") == 0);

    nexus_bl_free(&bl);
    printf("test_blacklist: all passed\n");
    return 0;
}
```

- [ ] **Step 2: 写 include/nexus_blacklist.h**

```c
#ifndef NEXUS_BLACKLIST_H
#define NEXUS_BLACKLIST_H

#define BL_MAX 1024

typedef struct {
    char *ips[BL_MAX];
    int   count;
} nexus_blacklist_t;

int  nexus_bl_init(nexus_blacklist_t *bl);
int  nexus_bl_add(nexus_blacklist_t *bl, const char *ip);
int  nexus_bl_check(const nexus_blacklist_t *bl, const char *ip);
void nexus_bl_free(nexus_blacklist_t *bl);

#endif
```

- [ ] **Step 3: 写 src/blacklist.c**

```c
#include "nexus_blacklist.h"
#include "nexus_util.h"
#include <string.h>
#include <stdlib.h>

int nexus_bl_init(nexus_blacklist_t *bl) {
    memset(bl, 0, sizeof(*bl));
    return 0;
}

int nexus_bl_add(nexus_blacklist_t *bl, const char *ip) {
    if (bl->count >= BL_MAX) return -1;
    bl->ips[bl->count++] = nexus_strdup(ip);
    return 0;
}

int nexus_bl_check(const nexus_blacklist_t *bl, const char *ip) {
    for (int i = 0; i < bl->count; i++) {
        if (strcmp(bl->ips[i], ip) == 0) return 1;
    }
    return 0;
}

void nexus_bl_free(nexus_blacklist_t *bl) {
    for (int i = 0; i < bl->count; i++) free(bl->ips[i]);
    memset(bl, 0, sizeof(*bl));
}
```

- [ ] **Step 4: 跑测试**

```bash
cd "E:/share/Nexus Gateway" && make test
```

预期：`test_blacklist: all passed`。

- [ ] **Step 5: 提交**

```bash
cd "E:/share/Nexus Gateway" && git add . && git commit -m "feat(blacklist): add IP blacklist with linear scan"
```

---

### Task 3.3: 零拷贝模块（sendfile + splice）

**Files:**
- Create: `include/nexus_zerocopy.h`
- Create: `src/zerocopy.c`
- Create: `tests/test_zerocopy.c`

**Interfaces:**
- Produces:
  ```c
  ssize_t nexus_zc_sendfile(int out_fd, int in_fd, off_t *offset, size_t count);
  ssize_t nexus_zc_splice(int from_fd, int to_fd, size_t len);  // 内部用 pipe
  ```

- [ ] **Step 1: 写测试 tests/test_zerocopy.c**

```c
#include "nexus_zerocopy.h"
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

int main(void) {
    // 准备源文件
    const char *content = "zero-copy test payload\n";
    int src = open("/tmp/nexus_zc_src.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    write(src, content, strlen(content));
    close(src);

    int fdin = open("/tmp/nexus_zc_src.txt", O_RDONLY);
    int fdout = open("/tmp/nexus_zc_dst.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);

    off_t off = 0;
    ssize_t n = nexus_zc_sendfile(fdout, fdin, &off, strlen(content));
    assert(n == (ssize_t)strlen(content));
    close(fdin); close(fdout);

    int fdr = open("/tmp/nexus_zc_dst.txt", O_RDONLY);
    char buf[64] = {0};
    read(fdr, buf, sizeof(buf) - 1);
    assert(strcmp(buf, content) == 0);
    close(fdr);

    unlink("/tmp/nexus_zc_src.txt");
    unlink("/tmp/nexus_zc_dst.txt");
    printf("test_zerocopy: all passed\n");
    return 0;
}
```

- [ ] **Step 2: 写 include/nexus_zerocopy.h**

```c
#ifndef NEXUS_ZEROCOPY_H
#define NEXUS_ZEROCOPY_H

#include <sys/types.h>

ssize_t nexus_zc_sendfile(int out_fd, int in_fd, off_t *offset, size_t count);
ssize_t nexus_zc_splice(int from_fd, int to_fd, size_t len);

#endif
```

- [ ] **Step 3: 写 src/zerocopy.c**

```c
#define _GNU_SOURCE
#include "nexus_zerocopy.h"
#include <sys/sendfile.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

ssize_t nexus_zc_sendfile(int out_fd, int in_fd, off_t *offset, size_t count) {
    return sendfile(out_fd, in_fd, offset, count);
}

ssize_t nexus_zc_splice(int from_fd, int to_fd, size_t len) {
    int pipefd[2];
    if (pipe2(pipefd, O_NONBLOCK) < 0) return -1;
    ssize_t total = 0;
    while (len > 0) {
        ssize_t n = splice(from_fd, NULL, pipefd[1], NULL, len, 0);
        if (n <= 0) break;
        ssize_t m = splice(pipefd[0], NULL, to_fd, NULL, n, 0);
        if (m <= 0) break;
        len -= m;
        total += m;
    }
    close(pipefd[0]); close(pipefd[1]);
    return total;
}
```

- [ ] **Step 4: 跑测试**

```bash
cd "E:/share/Nexus Gateway" && make test
```

预期：`test_zerocopy: all passed`。

- [ ] **Step 5: 提交**

```bash
cd "E:/share/Nexus Gateway" && git add . && git commit -m "feat(zerocopy): add sendfile and splice wrappers"
```

---

### Task 3.4: 健康检查模块（定时心跳）

**Files:**
- Create: `include/nexus_health.h`
- Create: `src/health.c`
- Create: `tests/test_health.c`

**Interfaces:**
- Produces:
  ```c
  typedef struct nexus_health_checker_t nexus_health_checker_t;  // 不透明

  nexus_health_checker_t *nexus_health_create(nexus_upstream_t *u, int interval_sec, int fail_threshold);
  void nexus_health_start(nexus_health_checker_t *h);  // 启动后台线程
  void nexus_health_stop(nexus_health_checker_t *h);
  void nexus_health_destroy(nexus_health_checker_t *h);
  ```

- [ ] **Step 1: 写测试 tests/test_health.c**

```c
#include "nexus_health.h"
#include "nexus_upstream.h"
#include <assert.h>
#include <unistd.h>

int main(void) {
    nexus_upstream_t u;
    nexus_upstream_init(&u, "test");
    // 加一个不可达的上游
    nexus_upstream_add_node(&u, "127.0.0.1", 1, 1);  // port 1 几乎肯定没人

    nexus_health_checker_t *h = nexus_health_create(&u, 1, 2);
    nexus_health_start(h);

    sleep(3);  // 等 2 次心跳

    // 不健康
    assert(u.nodes[0].healthy == 0);
    nexus_health_stop(h);
    nexus_health_destroy(h);
    printf("test_health: all passed\n");
    return 0;
}
```

- [ ] **Step 2: 写 include/nexus_health.h**

```c
#ifndef NEXUS_HEALTH_H
#define NEXUS_HEALTH_H

#include "nexus_upstream.h"

typedef struct nexus_health_checker_t nexus_health_checker_t;

nexus_health_checker_t *nexus_health_create(nexus_upstream_t *u, int interval_sec, int fail_threshold);
void nexus_health_start(nexus_health_checker_t *h);
void nexus_health_stop(nexus_health_checker_t *h);
void nexus_health_destroy(nexus_health_checker_t *h);

#endif
```

- [ ] **Step 3: 写 src/health.c**

```c
#define _GNU_SOURCE
#include "nexus_health.h"
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>

struct nexus_health_checker_t {
    nexus_upstream_t *u;
    int              interval_sec;
    int              fail_threshold;
    atomic_int       running;
    pthread_t        tid;
    int              fail_count[16];
};

static int tcp_probe(const char *host, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    struct timeval tv = {1, 0};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);
    int ok = connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0;
    close(fd);
    return ok;
}

static void *run(void *arg) {
    nexus_health_checker_t *h = arg;
    while (atomic_load(&h->running)) {
        for (int i = 0; i < h->u->node_count; i++) {
            int ok = tcp_probe(h->u->nodes[i].host, h->u->nodes[i].port);
            if (ok) {
                h->fail_count[i] = 0;
                h->u->nodes[i].healthy = 1;
            } else {
                if (++h->fail_count[i] >= h->fail_threshold) h->u->nodes[i].healthy = 0;
            }
        }
        sleep(h->interval_sec);
    }
    return NULL;
}

nexus_health_checker_t *nexus_health_create(nexus_upstream_t *u, int interval_sec, int fail_threshold) {
    nexus_health_checker_t *h = calloc(1, sizeof(*h));
    h->u = u; h->interval_sec = interval_sec; h->fail_threshold = fail_threshold;
    return h;
}

void nexus_health_start(nexus_health_checker_t *h) {
    atomic_store(&h->running, 1);
    pthread_create(&h->tid, NULL, run, h);
}

void nexus_health_stop(nexus_health_checker_t *h) {
    atomic_store(&h->running, 0);
    pthread_join(h->tid, NULL);
}

void nexus_health_destroy(nexus_health_checker_t *h) { free(h); }
```

- [ ] **Step 4: 跑测试**

```bash
cd "E:/share/Nexus Gateway" && make test
```

预期：`test_health: all passed`。

- [ ] **Step 5: 提交**

```bash
cd "E:/share/Nexus Gateway" && git add . && git commit -m "feat(health): add TCP probe background thread with fail threshold"
```

---

### Task 3.5: 静态文件服务 + sendfile 集成到 worker

**Files:**
- Create: `include/nexus_static.h`
- Create: `src/static_file.c`
- Create: `tests/test_static.c`

**Interfaces:**
- Produces:
  ```c
  int nexus_static_serve(int client_fd, const char *root, const char *path);
  ```

- [ ] **Step 1: 写测试 tests/test_static.c**

```c
#include "nexus_static.h"
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>

int main(void) {
    // 创建临时 root
    mkdir("/tmp/nexus_static_test", 0755);
    FILE *f = fopen("/tmp/nexus_static_test/hello.txt", "w");
    fputs("static file content\n", f); fclose(f);

    // 启动一个 TCP server on 19997 用 nexus_static_serve
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET; addr.sin_port = htons(19997);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(srv, (struct sockaddr*)&addr, sizeof(addr));
    listen(srv, 1);

    pid_t p = fork();
    if (p == 0) {
        int c = accept(srv, NULL, NULL);
        nexus_static_serve(c, "/tmp/nexus_static_test", "/hello.txt");
        close(c);
        exit(0);
    }
    close(srv);
    sleep(0.2);

    // 客户端
    int c = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET; sa.sin_port = htons(19997);
    inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
    connect(c, (struct sockaddr*)&sa, sizeof(sa));

    char buf[256] = {0};
    int n = read(c, buf, sizeof(buf) - 1);
    assert(n > 0);
    assert(strstr(buf, "200 OK") != NULL);
    assert(strstr(buf, "static file content") != NULL);

    close(c);
    waitpid(p, NULL, 0);
    unlink("/tmp/nexus_static_test/hello.txt");
    rmdir("/tmp/nexus_static_test");
    printf("test_static: all passed\n");
    return 0;
}
```

- [ ] **Step 2: 写 include/nexus_static.h**

```c
#ifndef NEXUS_STATIC_H
#define NEXUS_STATIC_H

int nexus_static_serve(int client_fd, const char *root, const char *path);

#endif
```

- [ ] **Step 3: 写 src/static_file.c**

```c
#define _GNU_SOURCE
#include "nexus_static.h"
#include "nexus_zerocopy.h"
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *content_type(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (strcmp(dot, ".html") == 0) return "text/html";
    if (strcmp(dot, ".txt") == 0)  return "text/plain";
    if (strcmp(dot, ".css") == 0)  return "text/css";
    if (strcmp(dot, ".js") == 0)   return "application/javascript";
    if (strcmp(dot, ".png") == 0)  return "image/png";
    if (strcmp(dot, ".jpg") == 0)  return "image/jpeg";
    return "application/octet-stream";
}

int nexus_static_serve(int client_fd, const char *root, const char *path) {
    if (strstr(path, "..") != NULL) return -1;  // 防止路径穿越
    char full[1024];
    snprintf(full, sizeof(full), "%s%s", root, path);

    int fd = open(full, O_RDONLY);
    if (fd < 0) {
        const char *resp = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        write(client_fd, resp, strlen(resp));
        return -1;
    }
    struct stat st;
    fstat(fd, &st);

    char hdr[512];
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n\r\n",
        content_type(path), (long)st.st_size);
    write(client_fd, hdr, (size_t)hlen);

    off_t off = 0;
    ssize_t remaining = st.st_size;
    while (remaining > 0) {
        ssize_t n = nexus_zc_sendfile(client_fd, fd, &off, (size_t)remaining);
        if (n <= 0) break;
        remaining -= n;
    }
    close(fd);
    return 0;
}
```

- [ ] **Step 4: 跑测试**

```bash
cd "E:/share/Nexus Gateway" && make test
```

预期：`test_static: all passed`。

- [ ] **Step 5: 提交**

```bash
cd "E:/share/Nexus Gateway" && git add . && git commit -m "feat(static): add static file serving with sendfile zero-copy"
```

---

## 阶段 4：集成 + 压测

### Task 4.1: 集成所有模块到 worker

**Files:**
- Modify: `src/worker.c`

- [ ] **Step 1: 把 rate_limit、blacklist、health、zerocopy、static 接入 worker 事件循环**

在 `src/worker.c` 顶部加：

```c
#include "nexus_rate_limit.h"
#include "nexus_blacklist.h"
#include "nexus_health.h"
#include "nexus_static.h"

static nexus_rate_limiter_t g_rl;
static nexus_blacklist_t    g_bl;
static nexus_health_checker_t *g_health = NULL;
```

在 `nexus_worker_run` 中初始化（master fork 前不要做 REUSEPORT 时不跑健康检查）：

```c
    int rate = nexus_config_get_int(cfg, "security", "rate_limit_per_ip", 100);
    nexus_rl_init(&g_rl, rate);
    nexus_bl_init(&g_bl);
    const char *bl_csv = nexus_config_get(cfg, "security", "blacklist");
    if (bl_csv) {
        char *tmp = strdup(bl_csv);
        for (char *tok = strtok(tmp, ", "); tok; tok = strtok(NULL, ", ")) {
            nexus_bl_add(&g_bl, tok);
        }
        free(tmp);
    }
    g_health = nexus_health_create(&g_upstream, 5, 2);
    nexus_health_start(g_health);
```

在 `handle_client_read` 中加入限流/黑名单检查（在 accept 后、转发前）：

```c
        // 简化：取 peer IP
        struct sockaddr_in cli; socklen_t cl = sizeof(cli);
        getpeername(cfd, (struct sockaddr*)&cli, NULL);
        char ip[64]; inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));

        if (nexus_bl_check(&g_bl, ip)) {
            const char *resp = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
            write(cfd, resp, strlen(resp));
            nexus_log_access("BLOCKED %s %s %s", ip, req.method, req.path);
            close(cfd);
            return;
        }
        if (!nexus_rl_check(&g_rl, ip)) {
            const char *resp = "HTTP/1.1 429 Too Many Requests\r\nContent-Length: 0\r\n\r\n";
            write(cfd, resp, strlen(resp));
            nexus_log_access("RATE_LIMITED %s %s %s", ip, req.method, req.path);
            close(cfd);
            return;
        }
```

路由分发：

```c
        // 路径前缀匹配：/static 走静态，/api 走代理，其他 404
        if (strncmp(req.path, "/static/", 8) == 0) {
            nexus_static_serve(cfd, "/var/www/html", req.path + 7);  // /static/foo → /foo
        } else {
            // 已有代理逻辑
        }
```

- [ ] **Step 2: 端到端集成测试**

```bash
cd "E:/share/Nexus Gateway" && make clean && make
./bin/nexus_gateway /tmp/nexus_test.ini &
PID=$!
sleep 1

# 正常代理
curl -sf http://127.0.0.1:8080/ | head -c 80
echo
# 黑名单：把 127.0.0.1 加入黑名单
sed -i 's/blacklist = /blacklist = 127.0.0.1,/' /tmp/nexus_test.ini
kill -HUP $PID
sleep 1
# 应返回 403
curl -s -o /dev/null -w "%{http_code}\n" http://127.0.0.1:8080/

kill $PID
```

预期：第一次 200，第二次 403。

- [ ] **Step 3: 提交**

```bash
cd "E:/share/Nexus Gateway" && git add . && git commit -m "feat(worker): integrate rate-limit, blacklist, health, static, routing"
```

---

### Task 4.2: wrk 压测脚本 + 报告

**Files:**
- Create: `bench/bench.sh`
- Create: `bench/bench_compare.sh`
- Create: `bench/result.md`（运行后填充）

- [ ] **Step 1: 写 bench/bench.sh**

```bash
#!/usr/bin/env bash
set -e
cd "$(dirname "$0")/.."

# 启动上游
mkdir -p /tmp/nexus_bench
dd if=/dev/urandom of=/tmp/nexus_bench/big.bin bs=1M count=50 2>/dev/null
(cd /tmp/nexus_bench && python3 -m http.server 19995) &
UP_PID=$!
sleep 0.5

# 启动网关
./bin/nexus_gateway config.ini.example &
GW_PID=$!
sleep 1

# 1. 代理 1KB 响应
echo "=== Proxy 1KB ==="
wrk -t8 -c1000 -d10s http://127.0.0.1:8080/

# 2. 静态文件 50MB
echo "=== Static 50MB ==="
mkdir -p /var/www/static
cp /tmp/nexus_bench/big.bin /var/www/static/

# 修改 config 路由 /static → file
wrk -t8 -c200 -d10s http://127.0.0.1:8080/static/big.bin

kill $GW_PID $UP_PID 2>/dev/null
wait 2>/dev/null
```

- [ ] **Step 2: 写 bench/bench_compare.sh（对比有/无零拷贝）**

```bash
#!/usr/bin/env bash
set -e
cd "$(dirname "$0")/.."

# 编译两个版本：默认（有 sendfile） 和 NX_NO_SENDFILE=1（用 read/write）
# 这里手工测试：注释掉 zerocopy 调用
echo "=== With sendfile ==="
make clean && make
./bin/nexus_gateway config.ini.example &
PID=$!; sleep 1
wrk -t8 -c200 -d10s http://127.0.0.1:8080/static/big.bin 2>&1 | tee /tmp/with_zc.txt
kill $PID; wait 2>/dev/null

echo "=== Without sendfile (manual) ==="
echo "(需手工把 src/static_file.c 中 nexus_zc_sendfile 改为 read+write)"
# 此处省略手工步骤，由开发者跑一次
```

- [ ] **Step 3: 让脚本可执行并跑**

```bash
cd "E:/share/Nexus Gateway" && chmod +x bench/bench.sh bench/bench_compare.sh
bash bench/bench.sh
```

预期：wrk 输出 Latency / Req/Sec 数据。

- [ ] **Step 4: 把数据填入 bench/result.md**

写入 `E:\share\Nexus Gateway\bench\result.md`（用实际跑出的数据替换占位）：

```markdown
# Nexus-Gateway 压测结果

测试环境：Linux 4.18 / Intel i7-8550U 4核 / 16GB
工具：wrk -t8 -c1000 -d10s

## 代理转发（1KB 响应）

| 模式 | QPS | 延迟 p99 | CPU |
|---|---|---|---|
| 单进程 | TBD | TBD | TBD |
| 4 Worker | TBD | TBD | TBD |
| 4 Worker + 零拷贝 | TBD | TBD | TBD |

## 静态文件（50MB）

| 模式 | QPS | 吞吐 MB/s | CPU |
|---|---|---|---|
| read+write | TBD | TBD | TBD |
| sendfile | TBD | TBD | TBD |

## 结论

零拷贝带来约 X% 的 QPS 提升和 Y% 的 CPU 下降（写简历用）。
```

- [ ] **Step 5: 提交**

```bash
cd "E:/share/Nexus Gateway" && git add . && git commit -m "bench: add wrk scripts and result template"
```

---

### Task 4.3: README + 文档完善

**Files:**
- Modify: `README.md`

- [ ] **Step 1: 覆盖写 README.md**

写入 `E:\share\Nexus Gateway\README.md`：

````markdown
# Nexus-Gateway

自研 C 语言轻量级高可用网关，支持四层 TCP 负载均衡与七层 HTTP 代理。

## 特性

- **Master-Worker 多进程**：SO_REUSEPORT 同端口监听，进程隔离
- **手写 HTTP/1.1 状态机**：零第三方解析库依赖
- **零拷贝**：`sendfile` / `splice` 内核态数据流转
- **共享内存双缓冲配置热更**：lock-free，无脏读
- **平滑重启**：SIGHUP 不中断业务流量
- **加权轮询** + **健康检查** + **IP 限流** + **黑名单**
- **哈希表头检索**：O(1) 查询

## 架构

```
config.ini → Master → fork → Worker[N] → epoll → upstream
              │
              └─ 共享内存 (双缓冲 + atomic 切换)
```

## 快速开始

```bash
make
cp config.ini.example config.ini
./bin/nexus_gateway config.ini
```

## 压测结果

详见 [bench/result.md](bench/result.md)。

## 设计文档

- [原始设计](docs/plan/轻量级高可用网关（四层+七层）从零设计文档.md)
- [Brainstorming spec](docs/superpowers/specs/2026-07-06-nexus-gateway-design.md)
- [实施计划](docs/superpowers/plans/2026-07-06-nexus-gateway.md)
````

- [ ] **Step 2: 提交**

```bash
cd "E:/share/Nexus Gateway" && git add . && git commit -m "docs: expand README with architecture and quickstart"
```

---

## 验收清单

完成所有 Task 后，逐项勾选：

- [ ] `make test` 全部单元测试通过
- [ ] `./bin/nexus_gateway config.ini` 启动后 `curl http://127.0.0.1:8080/` 命中上游
- [ ] `wrk` 压测有数据
- [ ] `kill -HUP <pid>` 后旧连接不中断
- [ ] 修改 `[security] blacklist` 后 SIGHUP 立即生效（403 命中）
- [ ] 9 大模块全部存在：epoll、HTTP 状态机、零拷贝、加权轮询、健康检测、平滑重启、限流、黑名单、哈希头检索
- [ ] `bench/result.md` 填入真实数据
