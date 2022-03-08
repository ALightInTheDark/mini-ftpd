// mini_ftpd
// Created by kiki on 2022/2/27.11:23
#pragma once
#include <sys/types.h>
#include <unordered_map>
using std::unordered_map;

extern void increase_connection(unsigned int ip);
extern void decrease_connection(pid_t pid);
extern void check_connection_limit(int conn_fd, unsigned int ip);

extern unordered_map<pid_t, unsigned int> pid2ip;