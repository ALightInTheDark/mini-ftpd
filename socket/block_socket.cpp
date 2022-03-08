// mini_ftpd
// Created by kiki on 2022/2/21.16:07
#include <unistd.h> // gethostname
#include <netdb.h> // gethostbyname
#include <fcntl.h>
#include <string.h> // strcpy
#include <stdlib.h>
#include "block_socket.h"

/*设置文件描述符为非阻塞模式*/
void activate_nonblock(int fd)
{
	int flag = fcntl(fd, F_GETFL);
	if(flag == -1) { perror("fcntl"); exit(EXIT_FAILURE); }

	flag |= O_NONBLOCK;

	int ret = fcntl(fd, F_SETFL, flag);
	if(ret == -1) { perror("fcntl"); exit(EXIT_FAILURE); }
}
/*取消文件描述符的非阻塞模式*/
void deactivate_nonblock(int fd)
{
	int flag = fcntl(fd, F_GETFL);
	if(flag == -1) { perror("fcntl"); exit(EXIT_FAILURE); }

	flag &= ~O_NONBLOCK;

	int ret = fcntl(fd, F_SETFL, flag);
	if(ret == -1) { perror("fcntl"); exit(EXIT_FAILURE); }
}

/*
阻塞IO超时时间的设置方法：
alarm(), 超时时间到后阻塞IO被闹钟信号打断,errno=EINTR；超时时间内收到数据，需要通过alarm(0)取消闹钟
setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO/SO_SNDTIMEO, timeout); 阻塞IO超时后返回-1，errno=ETIMEDOUT
select
*/
// 检测读操作是否超时.若超时则设置errno = ETIMEDOUT并返回-1，未超时返回0.
// wait_seconds为0时直接返回0.
int detect_read_timeout(int fd, unsigned int wait_seconds)
{
	if (wait_seconds <= 0) { return 0; }

	fd_set read_set;
	FD_ZERO(&read_set);
	FD_SET(fd, &read_set);

	struct timeval timeout;
	timeout.tv_sec = wait_seconds;
	timeout.tv_usec = 0;

	int ret;
	while ( (ret = select(fd + 1, &read_set, NULL, NULL, &timeout)) < 0 && errno == EINTR);
	if (ret == 0) { errno = ETIMEDOUT; return -1; }
	else if (ret == 1) { return 0; }
	else { perror("read_timeout::select"); return -1; }
}
// 检测写操作是否超时.若超时则设置errno = ETIMEDOUT并返回-1，未超时返回0
// wait_seconds为0时直接返回0.
int detect_write_timeout(int fd, unsigned int wait_seconds)
{
	if (wait_seconds <= 0) { return 0; }

	fd_set write_set;
	FD_ZERO(&write_set);
	FD_SET(fd, &write_set);

	struct timeval timeout;
	timeout.tv_sec = wait_seconds;
	timeout.tv_usec = 0;

	int ret;
	while ( (ret = select(fd + 1, NULL, &write_set, NULL, &timeout)) < 0 && errno == EINTR);
	if (ret == 0) { errno = ETIMEDOUT; return -1; }
	else if (ret == 1) { return 0; }
	else { perror("read_timeout::select"); return -1; }
}

// 默认情况下，客户端未收到对等方回应的SYN包，就会重发SYN请求。直到75秒后才会超时，返回ETIMEDOUT错误。
// 这个带超时的connect, 未超时连接到服务器，返回已连接socket fd; 超时设置 errno = ETIMEDOUT，返回-1; 出错返回-1。
// wait_seconds为0时，不检测超时。
int connect_with_timeout(int socket_fd, struct sockaddr_in* server_addr, unsigned int wait_seconds)
{
	if(wait_seconds > 0){ activate_nonblock(socket_fd); } // 在调用connect前将socket_fd设置为非阻塞，否则会阻塞在connect

	int ret = connect(socket_fd, (struct sockaddr*)server_addr, sizeof(struct sockaddr_in)); // 有可能连接直接建立成功
	if(ret < 0 && errno == EINPROGRESS)
	{
		fd_set connect_fdset;
		FD_ZERO(&connect_fdset);
		FD_SET(socket_fd, &connect_fdset);

		struct timeval timeout;
		timeout.tv_sec = wait_seconds;
		timeout.tv_usec = 0;

		while ( (ret = select(socket_fd + 1, NULL, &connect_fdset, NULL, &timeout)) == -1 && errno == EINTR);

		if(ret == 0)
		{
			deactivate_nonblock(socket_fd);
			errno = ETIMEDOUT;
			return -1;
		}
		else if(ret == -1)
		{
			deactivate_nonblock(socket_fd);
			perror("connect_with_timeout::select");
			return -1;
		}
		else if (ret == 1) // 套接字产生错误事件，也会产生可写事件。需要判断这种情况。
		{
			int err; socklen_t socklen = sizeof(err);
			int sockopt_ret = getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &err, &socklen);

			deactivate_nonblock(socket_fd);

			if(sockopt_ret == -1) { perror("connect_with_timeout::getsockopt"); return -1; }
			if(err == 0){ return 0; } // 没有错误发生，连接正常建立。
			else{ errno = err; return -1; }
		}
	}

	if(wait_seconds > 0) { deactivate_nonblock(socket_fd); }

	return ret;
}

// 带超时的accept.
// 在超时时间内接收到对等方的连接，返回已连接socket fd; 若超时则设置errno = ETIMEDOUT并返回-1。
// wait_second为0时直接调用accept()，不检测超时。
int accept_with_timeout(int listen_fd, struct sockaddr_in* client_addr, unsigned int wait_seconds)
{
	if (wait_seconds > 0)
	{
		fd_set accept_fdset;
		FD_ZERO(&accept_fdset);
		FD_SET(listen_fd, &accept_fdset);

		struct timeval timeout;
		timeout.tv_sec = wait_seconds;
		timeout.tv_usec = 0;

		int ret;
		while ( (ret = select(listen_fd + 1, &accept_fdset, NULL, NULL, &timeout)) == -1 && errno == EINTR);
		if (ret == -1) { return -1; }
		if (ret == 0) { errno = ETIMEDOUT; return -1; }
	}

	socklen_t len = sizeof (struct sockaddr_in);
	int conn_fd = client_addr == NULL ?
	accept(listen_fd, NULL, NULL):
	accept(listen_fd, (struct sockaddr*)client_addr, &len);
	if (conn_fd == -1) { perror("accept_with_timeout::accept");  }

	return conn_fd;
}

/*
创建客户端socket，绑定客户端ip地址和端口
port为0时，由内核选择ip地址和端口。
*/
int create_client_or_exit(unsigned short port)
{
	int socket_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socket_fd == -1) { perror("create_client_or_exit::socket"); exit(EXIT_FAILURE); }

	if (port != 0)
	{
		int on = 1;
		int ret = setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)); // 设置端口复用
		if (ret == -1) { perror("create_client_or_exit::setsockopt"); exit(EXIT_FAILURE); }

		struct sockaddr_in local_addr{};
		local_addr.sin_family = PF_INET;
		local_addr.sin_port = htons(port);
		char ip[16] = {0};
		get_local_ip(ip);
		local_addr.sin_addr.s_addr = inet_addr(ip);

		ret = bind(socket_fd, (struct sockaddr*)&local_addr, sizeof(struct sockaddr_in));
		if (ret == -1) { perror("create_client_or_exit::bind"); exit(EXIT_FAILURE); }
	}

	return socket_fd;
}
int create_client(const char* ip, unsigned short port)
{
	int socket_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socket_fd == -1) { perror("create_client_or_exit::socket"); return -1; }

	if (port != 0)
	{
		int on = 1;
		int ret = setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)); // 设置端口复用
		if (ret == -1) { perror("create_client_or_exit::setsockopt"); return -1; }

		struct sockaddr_in local_addr{};
		local_addr.sin_family = PF_INET;
		local_addr.sin_port = htons(port);
		local_addr.sin_addr.s_addr = inet_addr(ip);

		ret = bind(socket_fd, (struct sockaddr*)&local_addr, sizeof(struct sockaddr_in));
		if (ret == -1) { perror("create_client_or_exit::bind"); return -1; }
	}

	return socket_fd;
}
/*
* 创建监听socket，绑定服务器ip地址和端口
* host可以为ip地址、NULL。host为NULL时选取本机任意ip地址。
*/
int create_server_or_exit(const char* host, unsigned short port)
{
	int listen_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listen_fd == -1) { perror("create_server_or_exit::socket"); exit(EXIT_FAILURE); }

	struct sockaddr_in server_addr;
	server_addr.sin_family = PF_INET;
	if (host == NULL) { server_addr.sin_addr.s_addr = htonl(INADDR_ANY); } // htonl可不写，因为INADDR_ANY的值是0x00000000
	else
	{
		int ret = inet_aton(host, &server_addr.sin_addr);
		if (ret == 0) { fprintf(stderr, "create_server_or_exit::inet_aton failed\n"); exit(EXIT_FAILURE); }
	}
	server_addr.sin_port = htons(port);

	int on = 1;
	int ret = setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	if (ret == -1) { perror("create_server_or_exit::setsockopt"); exit(EXIT_FAILURE); }

	ret = bind(listen_fd, (struct sockaddr*)&server_addr, sizeof (struct sockaddr_in));
	if (ret == -1) { perror("create_server_or_exit::bind"); exit(EXIT_FAILURE); }

	ret = listen(listen_fd, SOMAXCONN);
	if (ret == -1) { perror("create_server_or_exit::listen"); exit(EXIT_FAILURE); }

	return listen_fd;
}

/*获取自己的网络地址信息*/
void get_local_addr(int socket_fd, struct sockaddr_in* addr)
{
	socklen_t len = sizeof (struct sockaddr_in);
	int ret = getsockname(socket_fd, (struct sockaddr*)addr, &len);
	if (ret == -1) { perror("get_local_addr::getsockname"); exit(EXIT_FAILURE); }
}
/*获取对等方的网络地址信息*/
void get_peer_addr(int socket_fd, struct sockaddr_in* addr)
{
	socklen_t len = sizeof (struct sockaddr_in);
	int ret = getpeername(socket_fd, (struct sockaddr*)addr, &len);
	if (ret == -1) { perror("getsockname"); exit(EXIT_FAILURE); }
}

void get_first_local_ip(char ip[16])
{
	char host[1024] = {0};
	int ret = gethostname(host, sizeof(host)); // 主机名类似于DESKTOP-GWREDGS
	if (ret == -1) { perror("gethostname"); exit(EXIT_FAILURE);}

	struct hostent* h = gethostbyname(host);
	if (h == NULL) { fprintf(stderr, "gethostbyname: %s \n", strerror(h_errno)); exit(EXIT_FAILURE);}

	strcpy(ip, inet_ntoa(*(struct in_addr*)h->h_addr));
}
#include <sys/ioctl.h>
#include <net/if.h>
/* 获取本机的ip地址, 成功返回0，失败返回-1 */
int get_local_ip(char ip[16])
{
	int sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(sockfd == -1) { perror("get_local_ip::socket"); return -1;}

	struct ifreq ireq{};
	strcpy(ireq.ifr_name, "eth0");

	ioctl(sockfd, SIOCGIFADDR, &ireq);

	struct sockaddr_in* host = (struct sockaddr_in*)&ireq.ifr_addr;
	strcpy(ip, inet_ntoa(host->sin_addr));

	close(sockfd);

	return 0;
}

/* 从阻塞fd中读取count字节到buf中 */
ssize_t read_n(int fd, void* buf, size_t count)
{
	size_t n_left = count; // 未读的字节数
	ssize_t n_read; // 已读的字节数
	char* buf_ptr = (char*)buf;

	while (n_left > 0)
	{
		if ((n_read = read(fd, buf_ptr, n_left)) < 0)
		{
			if (errno == EINTR) { continue; }
			else { perror("read_n::read"); return -1; }
		}
		else if (n_read == 0) { return count - n_left; } // 对等方关闭连接
		else
		{
			buf_ptr += n_read;
			n_left -= n_read;
		}
	}

	return count;
}
/* 向阻塞fd中写入count字节buf中的数据 */
ssize_t write_n(int fd, const void* buf, size_t count)
{
	size_t n_left = count;
	ssize_t n_writen;
	char* buf_ptr = (char*) buf;

	while (n_left > 0)
	{
		if ( (n_writen = write(fd, buf_ptr, n_left)) < 0)
		{
			if (errno == EINTR) { continue; }
			else { perror("write_n::write"); return -1; }
		}
		else if (n_writen == 0) { continue; } // write返回0, 并不代表对等方关闭连接
		else
		{
			buf_ptr += n_writen;
			n_left -= n_writen;
		}
	}

	return count;
}
ssize_t write_n(int fd, const char* str)
{ return write_n(fd, str, strlen(str)); }

/* 从sock fd偷看数据据到buf中，只要偷看到数据就返回, 最多偷看max_len字节数据 */
ssize_t recv_peek(int socket_fd, void* buf, size_t max_len)
{
	while (true)
	{
		ssize_t ret = recv(socket_fd, buf, max_len, MSG_PEEK);

		if (ret == -1 && errno == EINTR) { continue; }
		else { return ret; }
	}
}
/*
* 最多读取max_line-1字节到buf中，最后一个位置填\0; 如果读取到了\n, 在\n后填\0。
* 我们不会一次读取一个字符，这样会太频繁地进行read系统调用，效率很低
* 我们也不 将数据读到静态数组中进行缓存，从缓存中逐字符读取数据，判断是否有'\n'，因为这样的函数是不可重入的
*/
ssize_t readline(int socket_fd, void* buf, size_t max_len)
{
	size_t n_left = max_len - 1;
	char* buf_ptr = (char*)buf;

	while (n_left > 0)
	{
		ssize_t peek_ret = recv_peek(socket_fd, buf_ptr, n_left);
		if (peek_ret <= 0) { return peek_ret; } // 出错，或对等方关闭连接

		for (ssize_t i = 0; i < peek_ret; ++i)
		{
			if (buf_ptr[i] == '\n')
			{
				ssize_t ret = read_n(socket_fd, buf_ptr, i + 1); // 遇到'\n', 将数据从内核缓冲区读走
				if (ret != i + 1) { fprintf(stderr, "readline::read_n"); return -1; }
				buf_ptr += i + 1;
				*buf_ptr = '\0';
				return buf_ptr - (char*)0;
			}
		}

		if (peek_ret > n_left) { fprintf(stderr, "readline::recv_peek"); return -1; }
		ssize_t ret = read_n(socket_fd, buf_ptr, peek_ret); // 读走收到的字符,防止内核缓冲区满
		if (ret != peek_ret) { fprintf(stderr, "readline::read_n"); return -1; }

		n_left -= peek_ret;
		buf_ptr += peek_ret;
	}

	*buf_ptr = '\0';
	return max_len -1;
}

void send_char(int fd, char ch)
{
	ssize_t ret;
	while ( ( ret = write(fd, &ch, sizeof(char)) ) == -1 && errno == EINTR) { }

	if (ret != sizeof(char)) { perror("send_char::write"); }
}
char get_char(int fd)
{
	char res;
	ssize_t ret;
	while ( ( ret = read(fd, &res, sizeof(char)) ) == -1 && errno == EINTR) { }

	// if (ret == 0) { fprintf(stderr, "get_char::read : fd closed \n"); }
	//if (ret != sizeof(char)) { perror("get_char::read"); }

	return res;
}

void send_buf(int fd, const char* buf, unsigned int len)
{
	ssize_t ret = write_n(fd, &len, sizeof(unsigned int));
	if (ret != sizeof(unsigned int)) { fprintf(stderr, "send_buf failed \n"); exit(EXIT_FAILURE); }

	ret = write_n(fd, buf, len);
	if (ret != (ssize_t)len) { fprintf(stderr, "send_buf failed \n"); exit(EXIT_FAILURE); }
}

//接收一个整数
int get_int(int fd)
{
	int the_int;
	int ret = read_n(fd, &the_int, sizeof(the_int));
	if (ret != sizeof(the_int)) { fprintf(stderr, "get_int error\n"); exit(EXIT_FAILURE); }
	return the_int;
}
void recv_buf(int fd, char* buf, unsigned int len)
{
	unsigned int recv_len = get_int(fd);
	if (recv_len > len) { fprintf(stderr, "recv_buf error\n"); exit(EXIT_FAILURE); }
	int ret = read_n(fd, buf, recv_len);
	if (ret != (int)recv_len) { fprintf(stderr, "recv_buf error\n"); exit(EXIT_FAILURE); }
}

/*使fd能够接收带外数据*/
void activate_OutOfBind(int socket_fd)
{
	int on = 1;
	int ret = setsockopt(socket_fd, SOL_SOCKET, SO_OOBINLINE, &on, sizeof(on));
	if (ret == -1) { perror("setsockopt"); exit(EXIT_FAILURE); }
}
/*fd上有带外数据到来时，当前进程能够接收fd产生的SIGURG信号*/
void activate_SIGURG(int socket_fd)
{
	int ret = fcntl(socket_fd, F_SETOWN, getpid());
	if (ret == -1) { perror("fcntl"); exit(EXIT_FAILURE); }
}
