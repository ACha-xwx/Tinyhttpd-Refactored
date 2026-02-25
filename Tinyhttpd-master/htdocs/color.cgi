#!/usr/bin/perl -Tw
#
# 技术剖析：CGI 动态响应模块 (color.cgi)
# 本脚本配合 httpd.c 中的 execute_cgi 函数运行，
# 用于验证服务器对 POST/GET 参数的解析与管道回传能力。
#
# 核心安全特性：
# 1. -T (Taint Mode)：
#    强制开启污染检测。在 CGI 编程中，所有来自客户端的数据（如参数 'color'）
#    都被视为不安全。开启此模式可防止这些数据被非法执行，是生产环境的标配。
# 2. -w (Warnings)：
#    开启详细警告，用于捕获脚本运行时的潜在异常，辅助 httpd 进行错误溯源。

use strict;   # 强制执行严格的变量声明，规避全局变量污染
use CGI;      # 引入通用网关接口模块，处理底层的 HTTP 协议解析

# 实例化对象，自动解析来自 STDIN 或环境变量的请求数据
my($cgi) = new CGI;

/* * 协议规范：生成 HTTP 响应头
 * 脚本通过标准输出 (STDOUT) 将数据发回给 httpd 容器。
 * httpd 通过 dup2 重定向捕获这些输出并透传给浏览器。
 */
print $cgi->header;

# 初始化默认值：体现系统的鲁棒性，确保无参数输入时能正常降级显示
my($color) = "blue";

/* * 参数解析：数据流验证
 * 获取请求参数 'color'。此处的解析成功与否直接反映了 
 * httpd.c 中对 QUERY_STRING 或 CONTENT_LENGTH 的处理是否准确。
 */
$color = $cgi->param('color') if defined $cgi->param('color');

# 生成 HTML 动态页面
# 注意：BGCOLOR 的动态赋值验证了脚本对用户输入的实时响应能力
print $cgi->start_html(-title => uc($color),
                       -BGCOLOR => $color);

print $cgi->h1("This is $color");
print $cgi->p("此页面由重构后的 Tinyhttpd 容器通过管道 (Pipe) 通讯实时生成。");

# 结束 HTML 文档流
print $cgi->end_html;
