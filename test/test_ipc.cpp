// Test
// Created by kiki on 2022/2/26.9:56
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <socket_ipc.h>

int main()
{
	pair<int, int> fds = create_socketpair();

	pid_t pid = fork();
	if (pid == -1) { perror("fork"); exit(EXIT_FAILURE); }

	if (pid > 0) // 父进程
	{
		close(fds.second);

		int fd = recv_fd(fds.first); // 父进程收到打开的文件描述符

		char buf[1024] = {0};
		read(fd, buf, sizeof(buf));
		printf("%s", buf);
	}
	else
	{
		close(fds.first);

		int fd = open("/etc/mini_ftpd.conf", O_RDONLY);
		if (fd == -1) { perror("open"); exit(EXIT_FAILURE); }

		send_fd(fds.second, fd);
	}
}