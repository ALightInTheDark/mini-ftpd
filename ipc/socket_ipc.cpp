// mini_ftpd
// Created by kiki on 2022/2/22.15:07
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include "socket_ipc.h"

pair<int, int> create_socketpair()
{
	int sockfds[2];
	int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sockfds);
	if (ret == -1) { perror("socketpair"); exit(EXIT_FAILURE); }

	return {sockfds[0], sockfds[1]};
}
/*
ssize_t sendmsg(int sockfd, const struct msghdr* msg, int flags);
ssize_t recvmsg(int sockfd, struct msghdr* msg, int flags);

struct msghdr
{
	void*		  msg_name;       // 对等方的地址
	socklen_t     msg_namelen;    // 地址长度
	struct iovec* msg_iov;        // iovec数组
	size_t        msg_iovlen;     // iovec数组中元素的个数
	void*         msg_control;    // 指向要发送的辅助数据（cmsghdr结构体）
	size_t        msg_controllen; // 辅助数据的长度
	int           msg_flags;      // Flags (unused)
};

struct iovec
{
	void*  iov_base;    // 要发送数据所在的缓冲区
	size_t iov_len;     // 缓冲区的大小
};

struct cmsghdr // 辅助数据
{
	size_t cmsg_len; // 整个struct cmsghdr结构体的大小
	int    cmsg_level;  // Originating protocol
	int    cmsg_type;   // Prot ocol-specific type
	unsigned char cmsg_data[]; // 不定长数据
};
*/
/* 向socket发送 fd */
void send_fd(const int socket_fd, int send_fd)
{
	struct msghdr msg;

	char cmsgbuf[CMSG_SPACE(sizeof(send_fd))];
	msg.msg_control = cmsgbuf; // 这两行代码必须放在上面
	msg.msg_controllen = sizeof(cmsgbuf);  // 这两行代码必须放在上面

	struct cmsghdr* cmsg_ptr = CMSG_FIRSTHDR(&msg); // 由于内存对齐的影响，需要通过一系列宏来操作cmsghdr结构体
	cmsg_ptr->cmsg_len = CMSG_LEN(sizeof(send_fd));
	cmsg_ptr->cmsg_level = SOL_SOCKET;
	cmsg_ptr->cmsg_type = SCM_RIGHTS;
	int* fds_ptr = (int*)CMSG_DATA(cmsg_ptr);
	*fds_ptr = send_fd;

	struct iovec vec; // 我们只要发送辅助数据（即send_fd），因此iovec中只存放了一个无用的字节。
	char sendchar = 0;
	vec.iov_base = &sendchar;
	vec.iov_len = sizeof(sendchar);

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;
	msg.msg_flags = 0;

	ssize_t ret = sendmsg(socket_fd, &msg, 0);
	if (ret != 1) { perror("sendmsg"); exit(EXIT_FAILURE); }
}
/* 从socket fd接收fd并返回 */
int recv_fd(const int socket_fd)
{
	struct iovec vec;
	char recvchar = 0;
	vec.iov_base = &recvchar;
	vec.iov_len = sizeof(recvchar);

	int recv_fd;
	char cmsgbuf[CMSG_SPACE(sizeof(recv_fd))];

	struct msghdr msg;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;
	msg.msg_control = cmsgbuf;
	msg.msg_controllen = sizeof(cmsgbuf);
	msg.msg_flags = 0;

	int* fd_ptr = (int*)CMSG_DATA(CMSG_FIRSTHDR(&msg));
	*fd_ptr = -1;

	ssize_t ret = recvmsg(socket_fd, &msg, 0);
	if (ret != 1) { perror("recvmsg"); exit(EXIT_FAILURE); }

	struct cmsghdr* cmsg_ptr = CMSG_FIRSTHDR(&msg);
	if (cmsg_ptr == NULL) { perror("CMSG_FIRSTHDR"); exit(EXIT_FAILURE); }

	fd_ptr = (int*) CMSG_DATA(cmsg_ptr);
	recv_fd = *fd_ptr;
	if (recv_fd == -1) { perror("CMSG_DATA"); exit(EXIT_FAILURE); }

	return recv_fd;
}