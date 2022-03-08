// mini_ftpd
// Created by kiki on 2022/2/21.18:48
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <algorithm>
using std::ifstream, std::stringstream, std::getline;
using std::string; using std::cerr;
#include "config.h"

void Config::parse_config(const string& line)
{
	if (line.empty() || line.front() == '#') { return; } // 跳过注释行
	stringstream ss(line); // stringstream不支持字符串移动构造

	string config_item;
	char equal_sign;
	ss >> config_item >> equal_sign; // 当ss到流末尾后，>>操作不做任何事
	
	if (config_item == "pasv_mode") { ss >> pasv_mode; } // todo : 使用散列表实现配置项映射
	else if (config_item == "port_mode") { ss >> port_mode; }
	else if (config_item == "listen_port") { ss >> listen_port; }
	else if (config_item == "maximum_clients") { ss >> maximum_clients; }
	else if (config_item == "max_conns_per_ip") { ss >> max_conns_per_ip; }
	else if (config_item == "accept_timeout") { ss >> accept_timeout; }
	else if (config_item == "connect_timeout") { ss >> connect_timeout; }
	else if (config_item == "idle_ctrl_timeout") { ss >> idle_ctrl_timeout; }
	else if (config_item == "idle_data_timeout") { ss >> idle_data_timeout; }
	else if (config_item == "local_umask") { ss >> local_umask; }
	else if (config_item == "max_upload_rate") { ss >> max_upload_rate; max_upload_rate = std::max(1024*100u, max_upload_rate); }
	else if (config_item == "max_download_rate") { ss >> max_download_rate;  max_download_rate = std::max(1024*100u, max_download_rate); }
	else if (config_item == "listen_address") { ss >> listen_address; }
	else { cerr << "解析到未知的配置项! \n"; return; }
}

void Config::load_file(const char* path)
{
	ifstream fin(path);
	if (!fin) { cerr << "配置文件打开失败! \n"; return; }

	string line;
	while (getline(fin, line)) { parse_config(line); } // getline的第二个参数不能是stringstream

	print_config();
}

void Config::print_config() const
{
	cerr << "\n从配置文件中解析到以下配置项: \n";
	cerr << "pasv_mode=" << pasv_mode << "\n";
	cerr << "port_mode=" << port_mode << "\n";
	cerr << "listen_port=" << listen_port << "\n";
	cerr << "maximum_clients=" << maximum_clients << "\n";
	cerr << "max_conns_per_ip=" << max_conns_per_ip << "\n";
	cerr << "accept_timeout=" << accept_timeout << "\n";
	cerr << "connect_timeout=" << connect_timeout << "\n";
	cerr << "idle_ctrl_timeout=" << idle_ctrl_timeout << "\n";
	cerr << "idle_data_timeout=" << idle_data_timeout << "\n";
	cerr << "local_umask=" << local_umask << "\n";
	cerr << "max_upload_rate=" << max_upload_rate << "\n";
	cerr << "max_download_rate=" << max_download_rate << "\n";
	cerr << "listen_address=" << listen_address << "\n";
}

Config config;