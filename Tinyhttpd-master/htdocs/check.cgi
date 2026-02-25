#!/usr/bin/perl -Tw
# 
# 技术分析：CGI 安全性配置
# 1. 使用 -T (Taint mode) 标志：这是 Perl 的安全检查机制。
#    由于 Web 服务器 (httpd) 通过环境变量传递 QUERY_STRING，
#    开启 Taint 模式能防止非法的环境变量直接参与系统调用，有效防御命令注入。
# 2. 已适配标准 Linux 环境路径：确保在现代发行版下通过 Web 容器正确拉起解释器。
#

use strict;
use CGI;

my($cgi) = new CGI;

print $cgi->header('text/html');
print $cgi->start_html(-title => "Example CGI script",
                       -BGCOLOR => 'red');
print $cgi->h1("CGI Example");
print $cgi->p, "This is an example of CGI\n";
print $cgi->p, "Parameters given to this script:\n";
print "<UL>\n";
foreach my $param ($cgi->param)
{
 print "<LI>", "$param ", $cgi->param($param), "\n";
}
print "</UL>";
print $cgi->end_html, "\n";
