# Tinyhttpd-Refactored

这是一个基于经典 `Tinyhttpd` 的课程重构项目。我们的目标不是简单把源码跑通，而是在保留原有教学价值的前提下，补上它在现代环境里最容易出问题的几处短板，让它能更安全地处理请求，也能真正跑起一套完整的静态站点。

当前版本的核心改动集中在三块：

- 用 Redis 的 `SDS` 替换原版固定长度缓冲区，重写请求行读取逻辑，避免超长请求导致的缓冲区溢出。
- 接入 `C-Thread-Pool`，把原来的“每来一个连接就新建一个线程”改成线程池分发任务，减少高并发下的线程创建开销。
- 为现代静态站点补齐兼容性，包括中文 URL 解码、二进制资源正确发送，以及 `.webp`、`.svg`、`.ico` 等 MIME 类型支持。

项目里现在的 `htdocs` 已经不再是原版 tinyhttpd 自带的演示页面，而是一套实际部署过的个人博客静态资源，用它可以直接验证图片、脚本、样式和中文路径是否都能正常工作。

## 压测结果

为了避免把不同条件下的数据混在一起，我们保留了两组结果。

第一组是同一条命令下的横向对比：

```bash
ab -n 2000 -c 1000 -k http://127.0.0.1:4000/
```

| 指标 | 原版 Tinyhttpd | 重构版 |
| --- | --- | --- |
| RPS | 2298.44 [#/sec] | 9917.29 [#/sec] |
| 最长请求耗时 | 510 ms | 38 ms |
| 失败请求数 | 0 | 0 |

这组数据更适合说明重构前后在相同测试条件下的直接差异。

第二组是对重构版单独做的长时间压力测试：

```bash
ab -n 10000 -c 1000 http://127.0.0.1:4000/
```

- RPS: `13325.78 [#/sec]`
- 最长请求耗时: `47 ms`
- 失败请求数: `0`

这组数据更适合说明重构版在持续高并发下的稳定性。

相关结果文件保存在：

- [Results/Original_result_vs_refactored.txt](Results/Original_result_vs_refactored.txt)
- [Results/Refactored_result.txt](Results/Refactored_result.txt)

需要说明的是，压测采集于基准测试版本。后续我们又继续加入了博客页面和更多静态资源，因此当前 `htdocs` 中的首页内容，与压测时使用的首页资源并不完全相同。

## 目录说明

- [Tinyhttpd-master](Tinyhttpd-master) 是当前使用的重构版本。
- [Tinyhttpd-old](Tinyhttpd-old) 是保留的原始版本备份，主要用于对照。
- [Documents](Documents) 存放课程指导书和实践报告。
- [Results](Results) 存放压测结果。

## 构建方式

建议在 Linux 或 WSL 环境中构建：

```bash
cd Tinyhttpd-master
make
```

如果你想自行测试 CGI，需要另外准备可执行脚本和对应解释器环境。当前仓库重点保留的是服务器侧 CGI 处理逻辑，而不是原版 tinyhttpd 自带的 CGI 示例页面。

## 核心流程

当前版本的请求处理流程可以概括为：

1. 主线程启动服务器并监听端口。
2. 接收到连接后，不再直接 `pthread_create`，而是把 `client_sock` 投递到线程池。
3. 工作线程执行 `accept_request`，解析请求方法、URL 和查询参数。
4. 请求路径映射到 `htdocs` 下的资源；如果路径中包含中文，会先做 URL 解码。
5. 对于普通静态资源，直接回传文件；对于带查询参数的 GET、POST，或具有执行权限的文件，则进入 `execute_cgi`。
6. 静态文件通过二进制方式发送，避免图片和其它资源被文本读取逻辑截断。

## 代码位置

如果只看最关键的改动，可以从这几个文件开始：

- [Tinyhttpd-master/httpd.c](Tinyhttpd-master/httpd.c)
- [Tinyhttpd-master/sds.c](Tinyhttpd-master/sds.c)
- [Tinyhttpd-master/thpool.c](Tinyhttpd-master/thpool.c)
- [Tinyhttpd-master/Makefile](Tinyhttpd-master/Makefile)

## Credits

原始 tinyhttpd 由 J. David Blackstone 编写，版权说明与原项目保持一致。本仓库中的重构与扩展部分用于课程实践、源码分析和工程能力训练。
