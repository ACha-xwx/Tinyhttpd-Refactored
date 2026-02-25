/* simpleclient.c -- 简易 TCP 连通性测试工具
 * 作用: 一个最基础的 TCP 客户端。
 * 虽然主要用 ab 进行压力测试，但这个小工具用于验证 httpd 在重构后
 * 是否还能正常处理最基本的 Socket 连接（握手与回显）。
 */

#include <stdio.h>
#include <stdlib.h> 
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h> 

int main(int argc, char *argv[])
{
    // 这两个参数实际没用到，但在开启 -Wall 严格编译模式下会报错。
    // 这里显式转成 void，告诉编译器“我知道它们没用，别报错”，保持 0 Warning 交付。
    (void)argc;
    (void)argv;

    int sockfd;
    int len;
    struct sockaddr_in address;
    int result;
    char ch = 'A';

    // 创建流式套接字，走标准的 TCP 协议
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    
    address.sin_family = AF_INET;
    // 连接本地回环地址，方便调试
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    // 注意：这里的端口是 9734，如果 httpd 启动在随机端口或 4000，测试时需要对应修改
    address.sin_port = htons(9734);
    len = sizeof(address);

    // 发起连接请求
    result = connect(sockfd, (struct sockaddr *)&address, len);

    if (result == -1)
    {
        perror("oops: client1"); // 连不上直接报错，通常是因为服务器没跑起来
        exit(1);
    }
    
    // 最简单的 Ping-Pong 测试：发一个 'A'，看服务器能不能回一个
    // 如果这一步卡住，说明 httpd 的 recv/send 逻辑被改坏了
    write(sockfd, &ch, 1);
    read(sockfd, &ch, 1);
    printf("char from server = %c\n", ch);
    
    close(sockfd);
    exit(0);
}
