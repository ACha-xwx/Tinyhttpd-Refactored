#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdint.h>
#include "sds.h"  // 引入 SDS 库，替换掉原版那个不安全的 buf[1024]
#include "thpool.h"

#define ISspace(x) isspace((int)(x))
#define SERVER_STRING "Server: Tinyhttpd-Refactored/1.0.0\r\n"
#define THREAD_POOL_SIZE 4
#define STDIN   0
#define STDOUT  1
#define STDERR  2

void accept_request(void *);
void bad_request(int);
void cat(int, FILE *);
void cannot_execute(int);
void error_die(const char *);
void execute_cgi(int, const char *, const char *, const char *);
int get_line_sds(int, sds *);
void headers(int, const char *);
void not_found(int);
void serve_file(int, const char *);
int startup(u_short *);
void unimplemented(int);

/* * 重构后的读取行函数
 * 原版用固定长度 buf 很容易溢出，这里改用 sds 动态字符串。
 * sds 会自动扩容，理论上能读无限长的 Header，彻底解决了缓冲区溢出隐患。
 */
int get_line_sds(int sock, sds *line) {
    int i = 0;
    char c = '\0';
    int n;
    
    // 每次用之前必须清空，否则上次的数据会留着。
    // sdsclear 只是把长度设为0，不释放内存，这样下次循环能直接复用内存，效率更高。
    sdsclear(*line);

    while ((c != '\n')) {
        n = recv(sock, &c, 1, 0); // 一个个字节读，虽然慢点但能保证不读多
        if (n > 0) {
            if (c == '\r') {
                /* * 处理换行符的兼容性问题：
                 * 有的客户端是 \r\n，有的是 \r。
                 * 这里用 MSG_PEEK 偷看一下下一个字符，如果是 \n 就把它读出来扔掉。
                 */
                n = recv(sock, &c, 1, MSG_PEEK);
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            // 关键点：sdscatlen 会检查空间够不够，不够自动 realloc
            *line = sdscatlen(*line, &c, 1);
            i++;
        } else {
            c = '\n';
        }
    }
    return i;
}

// URL 解码函数：把浏览器发来的 %E6%96%B0 还原成中文
void urldecode(char *url) {
    char *p = url;
    char *decoded = url;
    while (*p) {
        if (*p == '%' && *(p + 1) && *(p + 2)) {
            int hex_val;
            sscanf(p + 1, "%2x", &hex_val);
            *decoded++ = (char)hex_val;
            p += 3;
        } else if (*p == '+') {
            *decoded++ = ' ';
            p++;
        } else {
            *decoded++ = *p++;
        }
    }
    *decoded = '\0';
}

/*
 * 处理 HTTP 请求的主入口
 * 主要逻辑：解析 Method -> URL -> QueryString -> 决定是返回静态文件还是运行 CGI
 */
void accept_request(void *arg) {
    int client = (intptr_t)arg;
    
    // 这里也换成了 sds，防止超长的请求行把栈撑爆
    sds buf = sdsempty();
    
    size_t numchars;
    char method[255];
    char url[255];
    char path[512];
    size_t i, j;
    struct stat st;
    int cgi = 0;      /* 0=静态，1=动态CGI */
    char *query_string = NULL;

    numchars = get_line_sds(client, &buf);

    // 提取 Method (GET/POST)
    // 虽然用了 SDS，但 method 这种标准字段还是限制一下长度比较安全
    i = 0; j = 0;
    while (!ISspace(buf[i]) && (i < sizeof(method) - 1)) {
        method[i] = buf[i];
        i++;
    }
    j=i;
    method[i] = '\0';

    if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) {
        unimplemented(client);
        sdsfree(buf); // 记得释放内存，防止泄漏
        return;
    }

    if (strcasecmp(method, "POST") == 0) cgi = 1;

    i = 0;
    while (ISspace(buf[j]) && (j < numchars)) j++;
    
    // 提取 URL
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < numchars)) {
        url[i] = buf[j];
        i++; j++;
    }
    url[i] = '\0';

    while (!isspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf)))
    {
        url[i++] = buf[j++];
    }
    url[i] = '\0';
    urldecode(url);

    // 如果是 GET 请求，还需要看看 url 里有没有带参数 (?)
    if (strcasecmp(method, "GET") == 0) {
        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0')) query_string++;
        if (*query_string == '?') {
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
    }

    // 拼凑实际文件路径，默认在 htdocs 目录下
    sprintf(path, "htdocs%s", url);
    if (path[strlen(path) - 1] == '/') strcat(path, "index.html");

    if (stat(path, &st) == -1) {
        // 文件找不到。注意：必须把 socket 里剩下的 header 读完才能报错，不然客户端会 reset
        while ((numchars > 0) && strcmp("\n", buf))
            numchars = get_line_sds(client, &buf);
        not_found(client);
    } else {
        if ((st.st_mode & S_IFMT) == S_IFDIR) {
            // 如果访问的是目录但没带斜杠，发送 301 重定向，强制浏览器加上 '/'
            char redirect_header[512];
            sprintf(redirect_header, "HTTP/1.1 301 Moved Permanently\r\nLocation: %s/\r\n\r\n", url);
            send(client, redirect_header, strlen(redirect_header), 0);
            
            sdsfree(buf);  // 释放资源
            close(client); // 断开连接，等待浏览器带上 '/' 重新发起请求
            return;
        }
        if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH)) cgi = 1;
        
        if (!cgi) serve_file(client, path);
        else execute_cgi(client, path, method, query_string);
    }

    sdsfree(buf); // 任务结束，回收
    close(client);
}

void bad_request(int client) {
    char buf[1024];
    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\nContent-type: text/html\r\n\r\n<P>Bad Request.\r\n");
    send(client, buf, strlen(buf), 0);
}

void cat(int client, FILE *resource)
{
    char buf[1024];
    size_t bytes_read;
    
    // 使用 fread 读取纯二进制数据，解决图片损坏问题
    while ((bytes_read = fread(buf, 1, sizeof(buf), resource)) > 0)
    {
        send(client, buf, bytes_read, 0);
    }
}

void cannot_execute(int client) {
    char buf[1024];
    sprintf(buf, "HTTP/1.0 500 Internal Error\r\nContent-type: text/html\r\n\r\n<P>CGI Error.\r\n");
    send(client, buf, strlen(buf), 0);
}

void error_die(const char *sc) {
    perror(sc);
    exit(1);
}

/*
 * 执行 CGI 脚本的核心逻辑
 * 用到了管道(Pipe)、Fork 和重定向(Dup2)这些 OS 核心机制。
 */
void execute_cgi(int client, const char *path, const char *method, const char *query_string) {
    char output[1024];
    sds line = sdsempty(); // 用 SDS 读 Header
    int cgi_output[2];     // 子进程写，父进程读
    int cgi_input[2];      // 父进程写，子进程读
    pid_t pid;
    int i, numchars = 1, content_length = -1;
    char c;

    // GET 请求丢弃 Header，POST 请求需要解析 Content-Length
    if (strcasecmp(method, "GET") == 0)
        while ((numchars > 0) && strcmp("\n", line)) numchars = get_line_sds(client, &line);
    else {
        numchars = get_line_sds(client, &line);
        while ((numchars > 0) && strcmp("\n", line)) {
            // 解析 Content-Length，注意这里用了 SDS 还是要小心指针操作
            if (strncasecmp(line, "Content-Length:", 15) == 0) content_length = atoi(&(line[16]));
            numchars = get_line_sds(client, &line);
        }
        if (content_length == -1) { bad_request(client); sdsfree(line); return; }
    }

    // 创建两个管道，如果失败就报错
    if (pipe(cgi_output) < 0 || pipe(cgi_input) < 0 || (pid = fork()) < 0) {
        cannot_execute(client); sdsfree(line); return;
    }

    sprintf(output, "HTTP/1.0 200 OK\r\n");
    send(client, output, strlen(output), 0);

    if (pid == 0) { /* 子进程：也就是 CGI 脚本 */
        char meth_env[255], query_env[255], length_env[255];
        
        // 把标准输入输出重定向到管道上
        // 这样脚本里 print 的东西就会发给浏览器，脚本从 stdin 读的就是 POST 数据
        dup2(cgi_output[1], STDOUT); 
        dup2(cgi_input[0], STDIN);
        close(cgi_output[0]); 
        close(cgi_input[1]);
        
        // 设置环境变量，这是 CGI 协议的标准传参方式
        sprintf(meth_env, "REQUEST_METHOD=%s", method); putenv(meth_env);
        if (strcasecmp(method, "GET") == 0) { 
            sprintf(query_env, "QUERY_STRING=%s", query_string); putenv(query_env); 
        } else { 
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length); putenv(length_env); 
        }
        
        // 修复点：POSIX 标准要求 execl 的第二个参数必须是文件名，最后必须是 NULL
        // 原版少了一个参数，会导致 segment fault
        execl(path, path, (char *)NULL); 
        exit(0);
    } else { /* 父进程：负责搬运数据 */
        close(cgi_output[1]); 
        close(cgi_input[0]);
        
        // 如果是 POST，把 socket 里收到的 Body 写给子进程
        if (strcasecmp(method, "POST") == 0)
            for (i = 0; i < content_length; i++) { 
                recv(client, &c, 1, 0); 
                write(cgi_input[1], &c, 1); 
            }
        
        // 从子进程读输出，发回给客户端
        while (read(cgi_output[0], &c, 1) > 0) send(client, &c, 1, 0);
        
        close(cgi_output[0]); 
        close(cgi_input[1]);
        waitpid(pid, NULL, 0); // 必须等待子进程结束，不然会变僵尸进程
    }
    sdsfree(line); // 记得释放
}

void headers(int client, const char *filename){
    char buf[1024];
    char *dot = strrchr(filename, '.'); // 寻找文件名后缀
    const char *type = "text/html";     // 默认类型

    // 智能识别文件类型 (MIME Type)
    if (dot != NULL) {
        if (strcmp(dot, ".css") == 0) type = "text/css";
        else if (strcmp(dot, ".js") == 0) type = "application/javascript";
        else if (strcmp(dot, ".png") == 0) type = "image/png";
        else if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0) type = "image/jpeg";
        else if (strcmp(dot, ".gif") == 0) type = "image/gif";
        else if (strcmp(dot, ".webp") == 0) type = "image/webp";
        else if (strcmp(dot, ".svg") == 0) type = "image/svg+xml";
        else if (strcmp(dot, ".ico") == 0) type = "image/x-icon";
    }

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: %s\r\n", type); // 动态输出类型
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

void not_found(int client) {
    char buf[1024];
    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n%sContent-Type: text/html\r\n\r\n<HTML><BODY><P>Not Found.</BODY></HTML>\r\n", SERVER_STRING);
    send(client, buf, strlen(buf), 0);
}

void serve_file(int client, const char *filename) {
    FILE *resource = NULL;
    int numchars = 1;
    sds buf = sdsempty();
    // 即使是静态文件，也要先把 Header 读完，不然协议流程不对
    while ((numchars > 0) && strcmp("\n", buf)) numchars = get_line_sds(client, &buf);
    
    resource = fopen(filename, "r");
    if (resource == NULL) not_found(client);
    else { headers(client, filename); cat(client, resource); }
    fclose(resource);
    sdsfree(buf);
}

int startup(u_short *port) {
    int httpd = 0;
    int on = 1;
    struct sockaddr_in name;
    
    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1) error_die("socket");
    
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    
    // 端口复用，调试的时候很有用，不然重启要等半天
    setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0) error_die("bind");
    
    // 如果传进来的端口是 0，就让系统随机分一个
    if (*port == 0) {
        socklen_t namelen = sizeof(name);
        getsockname(httpd, (struct sockaddr *)&name, &namelen);
        *port = ntohs(name.sin_port);
    }
    listen(httpd, 5);
    return(httpd);
}

void unimplemented(int client) {
    char buf[1024];
    sprintf(buf, "HTTP/1.0 501 Not Implemented\r\n%sContent-Type: text/html\r\n\r\n<P>Method Not Implemented.\r\n", SERVER_STRING);
    send(client, buf, strlen(buf), 0);
}

int main(void) {
    int server_sock = -1;
    u_short port = 4000;
    int client_sock = -1;
    struct sockaddr_in client_name;
    socklen_t client_name_len = sizeof(client_name);
    threadpool thpool = NULL;
    
    thpool = thpool_init(THREAD_POOL_SIZE);
    if (thpool == NULL) error_die("thpool_init");

    server_sock = startup(&port);
    printf("httpd running on port %d with %d worker threads\n", port, THREAD_POOL_SIZE);
    
    while (1) {
        client_sock = accept(server_sock, (struct sockaddr *)&client_name, &client_name_len);
        if (client_sock == -1) error_die("accept");
        
        if (thpool_add_work(thpool, accept_request, (void *)(intptr_t)client_sock) != 0) {
            close(client_sock);
            perror("thpool_add_work");
        }
    }

    thpool_destroy(thpool);
    close(server_sock);
    return(0);
}
