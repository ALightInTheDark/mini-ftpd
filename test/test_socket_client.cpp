// mini_ftpd
// Created by kiki on 2022/2/26.12:02
#include <block_socket.h>

int main()
{
	int connfd = create_client_or_exit(0);

	struct sockaddr_in serv_addr;
	serv_addr.sin_family = PF_INET;
	serv_addr.sin_port = htons(12345);
	serv_addr.sin_addr.s_addr = INADDR_ANY;

	int ret = connect_with_timeout(connfd, &serv_addr, 5);
	if (ret == -1 && errno == ETIMEDOUT) { printf("超时\n"); }
	if (ret == -1) { perror("connect_with_timeout"); } // 未启动服务器时直接启动客户端，connect连接被拒绝，产生可写事件
}