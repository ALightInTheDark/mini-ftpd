// mini_ftpd
// Created by kiki on 2022/2/21.17:04
#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include <string>
#include <vector>
#include <optional>
using std::string, std::vector, std::optional;
#include <sys/time.h>


struct Session // ftp服务进程保存、使用的上下文信息
{
	string cmd; // 用户输入的命令
	string arg; // 命令参数

	bool logged_in {false}; // 是否已登录

	bool active_port {false}; // 是否激活主动模式
	struct sockaddr_in port_addr{}; // 主动模式下，用户发来ip地址和端口，ftp服务进程将其保存在此变量中。
	void get_port_addr();

	bool active_pasv {false}; // 是否激活被动模式

	int control_fd; // 控制连接
	int data_fd {-1}; // 数据连接
	void reset_data_fd();

	int pic_fd {-1}; // ftp服务进程使用的socketpair fd

	bool ascii_mode {false}; // 数据传输格式。由子进程收到type命令后设置。本程序暂只支持二进制格式。

	optional<uid_t> uid; // 用户的uid。ftp服务进程在用户登录时获取其值。用于设置ftp服务进程的权限。

	long restart_position {0}; // 断点续传的位置。ftp服务进程收到REST命令后设置。ftp服务进程在每次文件传输时使用该值，接着立刻将其置为0.

	string rnfr_name; // 要重命名的文件名。ftp服务进程收到rnfr命令后设置。执行rnto命令后将其置空。


	bool received_abor {false}; // 是否收到abor命令。

	long time_point_sec {0}; // 传输开始的时间点,用作限速计算
	long time_point_usec {0};

	void init_transfer_start_time_point(); // 获取当前系统时间
	void limit_transmission_speed(ssize_t bytes_transfered, bool upload);

	Session(int conn_fd, int pic);

	void parse_command();
	[[nodiscard]] vector<string> split_arg() const;
};
