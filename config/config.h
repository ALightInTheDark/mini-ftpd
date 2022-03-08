// mini_ftpd
// Created by kiki on 2022/2/21.18:58
// 配置文件格式: accept_timeout = 600 等号左右必须有空格; 结尾不能有分号
#pragma once
#include <string>
using std::string;

struct Config
{
	bool port_mode {true}; // 是否开启主动模式。这个配置项暂时没有实现。
	bool pasv_mode {true}; // 是否开启被动模式。这个配置项暂时没有实现。
	string listen_address {"127.0.0.1"}; // 监听地址
	unsigned int listen_port {21}; // 控制连接的监听端口

	unsigned int accept_timeout {600}; // accept的超时时间
	unsigned int connect_timeout {300}; // connect的超时时间

	unsigned int maximum_clients {2000}; // 最大客户端连接数限制
	unsigned int max_conns_per_ip {50}; // 单个ip的最大连接数限制

	unsigned int max_upload_rate {0}; // 最大上传速度限制(单位为字节)
	unsigned int max_download_rate {0}; // 最大下载速度限制

	unsigned int idle_ctrl_timeout {300}; // 空闲控制连接的自动断开时间（单位为秒）
	unsigned int idle_data_timeout {300}; // 空闲数据连接的自动断开时间

	unsigned int local_umask {077}; // 创建文件时的umask

private:
	void parse_config(const string& line);
	void load_file(const char* path = "/etc/mini_ftpd.conf"); // 这里最好使用绝对路径
	void print_config() const;
public:
	Config() { load_file(); }
};

extern Config config;