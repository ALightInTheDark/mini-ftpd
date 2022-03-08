// mini_ftpd
// Created by kiki on 2022/2/26.12:02
#include <block_socket.h>

int main()
{
	int listen_fd = create_server_or_exit("127.0.0.1", 12345);
	int ret = accept_with_timeout(listen_fd, NULL, 10);
	if (ret == -1 && errno == ETIMEDOUT) { printf("accept超时! \n"); }

	char ip[16];
	get_local_ip(ip);
	printf("%s \n", ip);
	get_first_local_ip(ip);
	printf("%s \n", ip);
}