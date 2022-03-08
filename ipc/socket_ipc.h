// mini_ftpd
// Created by kiki on 2022/2/22.15:07
#pragma once
#include <utility>
using std::pair;

extern pair<int, int> create_socketpair(); // 创建socketpair用于父子进程间通信。（如果要在不相干的进程间通信，需要使用unix域协议。）
extern void send_fd(int socket_fd, int fd); // 在父子进程间，通过socketpair传递文件描述符。
extern int recv_fd(int socket_fd);

