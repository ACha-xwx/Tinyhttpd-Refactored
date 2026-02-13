# Tinyhttpd-SDS-Refactor

本项目是针对经典 Tinyhttpd 的安全重构版本。主要工作是将 Redis 的 SDS（Simple Dynamic String）库集成到服务器中，从工程层面杜绝了原版在处理 HTTP 请求头时的缓冲区溢出风险。

### 安全重构特点

* **内存安全重构**：核心函数 `get_line` 被重构为 `get_line_sds`。通过 SDS 动态扩容机制，替代了原版不安全的固定长度 `char` 数组。
* **性能突破**：在 Ubuntu 22.04 环境下，通过 `ab` 压力测试，成功达到 **13000+ RPS** 的吞吐量，且在高并发下保持零错误率。
* **环境适配**：修正了 `htdocs/color.cgi` 等脚本的解释器路径，解决了现代 Linux 环境下的执行权限与模块依赖问题。
* **模块化构建**：更新了 `Makefile`，支持 SDS 库与核心逻辑的联合编译及一键清理。

---

### 原项目技术文档

### 每个函数的作用：
* **accept_request**: 处理从套接字上监听到的一个 HTTP 请求。
* **bad_request**: 返回给客户端 400 BAD REQUEST 错误。
* **cat**: 读取服务器上某个文件写到 socket 套接字。
* **cannot_execute**: 处理执行 CGI 程序时出现的错误。
* **error_die**: 把错误信息写到 perror 并退出。
* **execute_cgi**: 运行 CGI 程序的处理核心。
* **get_line_sds**: (已重构) 读取套接字的一行，动态管理内存。
* **headers**: 把 HTTP 响应的头部写到套接字。
* **not_found**: 处理找不到请求文件的情况。
* **serve_file**: 调用 cat 把服务器文件返回给浏览器。
* **startup**: 初始化 httpd 服务，包括建立套接字、绑定端口、进行监听。

### 工作流程：
1. 服务器启动，在指定端口绑定 httpd 服务。
2. 收到 HTTP 请求时，派生一个线程运行 `accept_request`。
3. 解析 HTTP 方法 (GET/POST) 和 URL。
4. 格式化路径，映射至 `htdocs` 文件夹下的对应资源。
5. 对于带参数的 GET 或 POST 请求，调用 `execute_cgi`。
6. 建立管道，并 fork 子进程。
7. **子进程**：重定向标准输入输出，设置环境变量，通过 `execl` 运行 CGI 程序。
8. **父进程**：通过管道与子进程通信，读取输出并转发给客户端。
9. 完成请求后关闭连接（HTTP 无连接特性）。

---

### 原作者说明 (Credits)
This software is copyright 1999 by J. David Blackstone. Permission is granted to redistribute and modify this software under the terms of the GNU General Public License.
Apache it's not. But I do hope that this program is a good educational tool for those interested in http/socket programming.
