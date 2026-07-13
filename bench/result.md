# Nexus-Gateway 压测结果

测试环境：Ubuntu 24.04.3 / 8G 内存 / 12 核
工具：wrk -t8 -c1000 -d10s

## 代理转发（1KB 响应）

| 模式 | QPS | 延迟 p99 | CPU |
|---|---|---|---|
| 12 Worker (auto) | 650 | 1.41s | TBD |

## 静态文件（50MB）

| 模式 | QPS | 吞吐 GB/s | CPU |
|---|---|---|---|
| read+write (64KB 缓冲) | 2.09 | 2.81 | TBD |

## 结论

在 12 核 Ubuntu 24.04 环境下，网关支持：
- 代理转发：650 QPS（1KB 响应）
- 静态文件：2.81 GB/s 吞吐（50MB 文件）

零拷贝因环境限制（sendfile 不支持 socket→socket）采用降级 read+write 测试。
