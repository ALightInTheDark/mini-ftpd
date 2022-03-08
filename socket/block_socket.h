// mini_ftpd
// Created by kiki on 2022/2/21.16:07
#pragma once
#include <sys/socket.h> // socket, setsockopt, bind, listen, recv
#include <netinet/in.h> // sockaddr_in
#include <arpa/inet.h> // inet_ntoa
#include <errno.h>
#include <stdio.h>

extern void activate_nonblock(int fd);
extern void deactivate_nonblock(int fd);

extern int detect_read_timeout(int fd, unsigned int wait_seconds);
extern int detect_write_timeout(int fd, unsigned int wait_seconds);
extern int connect_with_timeout(int socket_fd, struct sockaddr_in* server_addr, unsigned int wait_seconds);
extern int accept_with_timeout(int listen_fd, struct sockaddr_in* client_addr, unsigned int wait_seconds);

extern int create_server_or_exit(const char* host, unsigned short port);
extern int create_client_or_exit(unsigned short port);
extern int create_client(const char* ip, unsigned short port);

void get_local_addr(int socket_fd, struct sockaddr_in* addr);
void get_peer_addr(int socket_fd, struct sockaddr_in* addr);
extern int get_local_ip(char ip[16]);
extern void get_first_local_ip(char ip[16]);

extern ssize_t read_n(int fd, void* buf, size_t count);
extern ssize_t write_n(int fd, const void* buf, size_t count);
extern ssize_t write_n(int fd, const char* str);

extern ssize_t recv_peek(int socket_fd, void* buf, size_t max_len);
extern ssize_t readline(int socket_fd, void* buf, size_t max_len);

extern void send_char(int fd, char ch);
extern char get_char(int fd);

extern void send_buf(int fd, const char* buf, unsigned int len); // 首先发送buf的长度len, 接着发送buf.
extern void recv_buf(int fd, char* buf, unsigned int len);

extern void activate_OutOfBind(int socket_fd);
extern void activate_SIGURG(int socket_fd);