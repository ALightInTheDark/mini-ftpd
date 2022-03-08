// mini_ftpd
// Created by kiki on 2022/2/27.11:23
#include <unistd.h>
#include "connection_limit.h"
#include "./config/config.h"
#include "./socket/block_socket.h"

size_t client_count = 0; // 已连接客户端数量，用于限制客户端的连接数
unordered_map<unsigned int, int> ip2conncount; // ip地址 -> ip地址对应的连接数
unordered_map<pid_t, unsigned int> pid2ip; // nobody进程的pid -> 为哪个ip服务

void increase_connection(unsigned int ip)
{
	++client_count;
	++ip2conncount[ip];
}

void decrease_connection(pid_t pid)
{
	auto iter = pid2ip.find(pid);
	if (iter == pid2ip.end()) { fprintf(stderr, "cannot find pid in pid2ip hashtable! \n"); }

	auto it = ip2conncount.find(iter->second);
	if (it == ip2conncount.end()) { fprintf(stderr, "cannot find ip in ip2conncount hashtable! \n"); }
	else if (it->second <= 0) { fprintf(stderr, "connection count less than one! \n"); }
	else if (it->second == 1) { ip2conncount.erase(it); }
	else { --it->second; }

	pid2ip.erase(iter);

	--client_count;
}

void check_connection_limit(int conn_fd, unsigned int ip)
{
	if (config.maximum_clients > 0 && client_count > config.maximum_clients)
	{
		write_n(conn_fd, "421 There are too many connected users. please try later.\r\n");
		exit(EXIT_SUCCESS);
	}
	if (config.max_conns_per_ip > 0 && ip2conncount[ip] > config.max_conns_per_ip)
	{
		write_n(conn_fd, "421 There are too many connections from your internet address.\r\n");
		exit(EXIT_SUCCESS);
	}
}
