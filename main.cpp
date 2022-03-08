#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> // getuid
#include <signal.h>
#include <sys/wait.h>

#include "block_socket.h"
#include "session.h"
#include "connection_limit.h"
#include "ftp_protocol.h"
#include "config.h"

void sigchld_handler(int)
{
	pid_t pid;
	while ( (pid = waitpid(-1, NULL, WNOHANG)) > 0) { decrease_connection(pid); }
}

int main()
{
	if (getuid()!= 0) { fprintf(stderr, "mini ftpd 必须以root权限运行! \n"); exit(EXIT_FAILURE); } // 只有root用户才能访问shadow文件

	// int ret = daemon(1, 1);
	// if (ret == -1) { perror("daemon"); exit(EXIT_FAILURE); }

	if (signal(SIGCHLD, sigchld_handler) == SIG_ERR) { perror("signal"); exit(EXIT_FAILURE); }

	int listen_fd = create_server_or_exit(config.listen_address.data(), config.listen_port);

	while (true)
	{
		struct sockaddr_in client_addr{};
		int conn_fd = accept_with_timeout(listen_fd, &client_addr, 0); // 不使用IO复用的多进程模型下，使用非阻塞IO是没有意义的；我们使用阻塞IO。
		if (conn_fd == -1) { perror("accept"); exit(EXIT_FAILURE); }

		increase_connection(client_addr.sin_addr.s_addr);

		pid_t pid = fork();
		if (pid == -1) { perror("fork"); exit(EXIT_FAILURE); }

		if (pid == 0) // 子进程为客户端提供服务
		{
			check_connection_limit(conn_fd, client_addr.sin_addr.s_addr);

			if (signal(SIGCHLD, SIG_IGN)== SIG_ERR) { perror("signal"); exit(EXIT_FAILURE); } // 子进程恢复SIGCHLD的默认处理, 之后子进程也会调用fork()

			close(listen_fd);

			start_ftp_service(conn_fd);
		}
		else // 父进程继续接收其它客户端的连接
		{
			pid2ip[pid] = client_addr.sin_addr.s_addr; // 注意，子进程和父进程各有一份哈希表。该操作必须在父进程的哈希表中完成。

			close(conn_fd);
		}
	}
}
