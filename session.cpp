// mini_ftpd
// Created by kiki on 2022/2/21.17:04
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sstream>
#include <algorithm>
using std::istringstream;
using std::transform;
#include <block_socket.h>
#include <process_utility.h>
#include <config.h>
#include <string.h>
#include "session.h"

Session::Session(int conn_fd, int pic) : control_fd(conn_fd), pic_fd(pic) { }

void Session::parse_command()
{
	char buf[1024];
	ssize_t ret = readline(control_fd, buf,  sizeof(buf));
	if (ret == -1) { perror("Session::readcmd"); exit(EXIT_FAILURE); }
	else if (ret == 0) { fprintf(stderr, "客户端断开连接, FTP服务进程退出\n"); exit(EXIT_SUCCESS); } // 对等方关闭连接

	string cmdline(buf);
	istringstream ss(cmdline);
	ss >> cmd;
	transform(cmd.begin(), cmd.end(), cmd.begin(),::toupper);
	if (cmd != "SITE") { arg.clear(); ss >>arg; }
	else { std::getline(ss, arg); }

	fprintf(stderr, "客户端发送的命令是%s参数是%s\n", cmd.data(), arg.data());
}

void Session::reset_data_fd()
{
	close(data_fd);
	data_fd = -1;
	active_pasv = false;
	active_port = false;
}

void Session::get_port_addr() // 127,0,0,1,123,456 字符串形式的地址无须进行字节序转换
{
	istringstream ss(arg); // sscanf "%u,%u,%u,%u,%u,%u"
	unsigned int v[6]; // 注意，此处不能用unsigned char, 因为每个单元是8字节的
	for (int i = 0; i < 6; ++i)
	{
		ss >> v[i];
		if (i <= 4)ss.ignore();
		if (i <= 4 && !ss.good()) { fprintf(stderr, "PORT command missing args \n"); }
	}

	port_addr.sin_family = PF_INET;
	auto* p = (unsigned char*)&port_addr.sin_addr.s_addr;
	p[0] = v[0];
	p[1] = v[1];
	p[2] = v[2];
	p[3] = v[3];
	p = (unsigned char*)&port_addr.sin_port;
	p[0] = v[4]; // 这里不需要字节序转换，因为我们是通过指针按字节操作的。
	p[1] = v[5];

	active_port = true;
}

void Session::init_transfer_start_time_point() // 获取当前系统时间
{
	struct timeval current_time{};
	int ret = gettimeofday(&current_time, NULL);
	if (ret == -1) { perror("gettimeofday"); return; }

	time_point_sec = current_time.tv_sec;
	time_point_usec = current_time.tv_usec;
}

// 睡眠时间 =（当前传输速度 / 最大传输速度 - 1）* 当前传输时间
static struct timeval current_time;
void Session::limit_transmission_speed(ssize_t bytes_transfered, bool upload) // todo: 类型转换带来大量的进度损失，导致速度下降不平滑。
{
	int ret = gettimeofday(&current_time, NULL);
	if (ret == -1) { perror("gettimeofday"); return; }

	double elapsed;
	elapsed = (double)current_time.tv_sec - (double)time_point_sec + (double)(current_time.tv_usec - time_point_usec) / (double)1000000;
	if (elapsed <= 0.0) { elapsed = (double)0.01; }
	
	// 计算当前传输速度
	auto current_rate = (unsigned int)((double)bytes_transfered / elapsed);
	unsigned max_rate = upload ? config.max_upload_rate : config.max_download_rate;
	
	if (current_rate > max_rate)
	{
		double rate_ratio = (double)current_rate / max_rate; // 计算速度的比率
		double pause_time = (rate_ratio -(double)1) * elapsed;
		sleep_for_seconds(pause_time);
	}

	// 更新下一次开始传输的时间
	ret = gettimeofday(&current_time, NULL);
	if (ret == -1) { perror("gettimeofday"); return; }
	time_point_sec = current_time.tv_sec;
	time_point_usec = current_time.tv_usec;
}

vector<string> split(const string& s, const string& delimiters = " ");
vector<string> Session::split_arg() const { return split(arg); }

vector<string> split(const string& s, const string& delimiters)
{
	vector<string> tokens;

	auto startFind_pos = s.find_first_not_of(delimiters, 0);
	auto found_pos = s.find_first_of(delimiters, startFind_pos); // 不能是startFind_pos + 1, 可能会out_of_range

	while (startFind_pos != string::npos || found_pos != string::npos)
	{
		tokens.push_back(s.substr(startFind_pos, found_pos - startFind_pos));

		startFind_pos = s.find_first_not_of(delimiters, found_pos);
		found_pos = s.find_first_of(delimiters, startFind_pos);
	}

	return tokens;
}