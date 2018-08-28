# 文件服务器
 - 实现仿云盘的文件传输服务


## 项目概览
- 使用半同步半异步进程池搭建文件传服务框架
- 仿写及简化Nginx内存池实现文件传输的内存高速开辟
- 使用STL unordered_map 管理用户的文件记录信息

## 项目工具
- Linux centos7 , vim , gcc, g++, gdb , git

## 项目时间
- 2017/9/4 - 2017/11/1

## 目录结构
![](https://i.imgur.com/qSa5CWo.png)

## 功能简介
- client/cli.cpp 一个简单的客户端实现
- client/include 客户端的头文件结构
- client/sockpair 双工管道的封装
- Nginx/myalloctor.h 空间配置器
- nginx/Nginxxx.h Nginx内存池的实现
- server/proess/Dir.h ser的目文件录管理结构
- server/process/FTP.h ser的进程池实现
- server/process/process.cpp ser的业务代码

## 代码统计
![](https://i.imgur.com/1AmQJe7.png)

