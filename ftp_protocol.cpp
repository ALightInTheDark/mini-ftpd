// mini_ftpd
// Created by kiki on 2022/2/21.22:03
#include <pwd.h> // getpwnam, getpwuid
#include <shadow.h> // getspnam
#include <crypt.h> // crypt, 链接选项-lcrypt
#include <unistd.h> // setegid, seteuid
#include <fcntl.h> // open
#include <signal.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/stat.h> // mkdir
#include <sys/sendfile.h> // sendfile
#include <string>
#include <unordered_map>
using std::string, std::unordered_map;
#include "session.h"
#include <cassert>
#include <process_utility.h>
#include <block_socket.h>
#include <file_utility.h>
#include <socket_ipc.h>
#include <config.h>
#include "ftp_protocol.h"

Session* session_ptr;
void control_alarm_handler(int) // 控制连接超时
{
	fprintf(stderr, "控制连接超时!\n");
	shutdown(session_ptr->control_fd, SHUT_RD);
	write_n(session_ptr->control_fd, "421 Timeout.\r\n");
	shutdown(session_ptr->control_fd, SHUT_WR);
	exit(EXIT_SUCCESS);
}
void kill_idle_coltrol_conn()
{
	if (config.idle_ctrl_timeout > 0)
	{
		signal(SIGALRM, control_alarm_handler);
		alarm(config.idle_ctrl_timeout); // 如果多次调用alarm()，新的闹钟时间会覆盖旧的。
	}
}


void sigurg_handler(int) // 收到sigurg信号时，关闭数据连接。在用户登录后注册此信号。
{
	if (session_ptr->data_fd == -1) { return; }

	session_ptr->parse_command();
	if (session_ptr->cmd == "ABOR" || session_ptr->cmd == "\377\364\377\362ABOR")
	{
		session_ptr->received_abor = true;
		shutdown(session_ptr->data_fd, SHUT_RDWR);
	}
	else { write_n(session_ptr->control_fd, "500 Unkonwn command.\r\n"); }
}

#define CONNECT_IN_ACTV 1
#define ACCEPT_IN_PASV 2
#define LISTEN_IN_PASV 3
#define result_ok 1
#define result_bad 2
extern void connect_in_actv(int nobody_pic_fd);
extern void listen_in_pasv(int nobody_pic_fd, int& pasv_listen_fd);
extern void accept_in_pasv(int nobody_pic_fd, int pasv_listen_fd);

extern unordered_map<string, void(*)(Session& session)> services;
void start_ftp_service(int conn_fd)
{
	activate_OutOfBind(conn_fd);

	pair<int, int> fds = create_socketpair(); // 父进程使用fds.first, 子进程使用fds.second

	pid_t pid = fork();
	if (pid == -1) { perror("fork"); exit(EXIT_FAILURE); }

	if (pid == 0) // 子进程成为ftp服务进程
	{
		Session session(conn_fd, fds.second);
		close(fds.first);
		session_ptr = &session;

		write_n(session.control_fd, "220 (mini ftpd version 1.0)\r\n");

		while (true)
		{
			kill_idle_coltrol_conn(); // 每次阻塞接收客户端命令前，重新注册sigalrm信号。

			session.parse_command(); // 客户端断开连接，则退出
			if (!session.logged_in && session.cmd != "USER" && session.cmd != "PASS")
			{
				write_n(session.control_fd, "500 Please login first\r\n");
				continue;
			}

			alarm(0); // 接收到命令后，取消sigalrm信号。若要在命令执行时间过长时也断开控制连接，则不需要这一行。

			auto iter = services.find(session.cmd);
			if (iter == services.end()) { write_n(session.control_fd, "500 Unknown command.\r\n"); }
			else if(iter->second == NULL){ write_n(session.control_fd, "502 Unimplement command.\r\n"); }
			else {( iter->second)(session); }
		}
	}
	else // 父进程成为nobody进程, 辅助子进程与客户端建立数据连接。
	{
		int nobody_pic_fd = fds.first;
		close(fds.second);
		int pasv_listen_fd = -1; // 被动模式下监听客户连接的socket fd

		create_nobody_process();

		while (true)
		{
			char cmd = get_char(nobody_pic_fd);
			if (cmd == 0) { fprintf(stderr, "ftp服务进程退出, nobody进程也随之退出.ftp服务进程的进程号是%d, nobody进程的进程号是%d\n", pid, getpid()); exit(EXIT_SUCCESS); }

			switch (cmd)
			{
				case CONNECT_IN_ACTV: connect_in_actv(nobody_pic_fd); break;
				case LISTEN_IN_PASV: listen_in_pasv(nobody_pic_fd, pasv_listen_fd); break;
				case ACCEPT_IN_PASV: accept_in_pasv(nobody_pic_fd, pasv_listen_fd); break;
				default: fprintf(stderr, "start_ftp_service: unknown inner cmd %d\n", cmd);
			}
		}
	}
}

void connect_in_actv(int nobody_pic_fd) // nobody进程ftp服务进程读取用户提供的端口号和ip地址，主动连接到用户; 在主动模式下使用
{
	int data_fd = create_client(config.listen_address.data(), 20);  // 绑定20端口
	if (data_fd == -1) { perror("socket then bind"); send_char(nobody_pic_fd, result_bad); return; }

	struct sockaddr_in addr;
	recv_buf(nobody_pic_fd, (char*)&addr, sizeof(addr)); // 也可以分别接收端口和字符串形式的IP地址。

	int ret = connect_with_timeout(data_fd, &addr, config.connect_timeout);
	if (ret < 0) { perror("connect"); printf("errno=%d\n", errno); close(data_fd); send_char(nobody_pic_fd, result_bad); return; }

	send_char(nobody_pic_fd, result_ok);

	send_fd(nobody_pic_fd, data_fd);

	close(data_fd);
}

void listen_in_pasv(int nobody_pic_fd, int& pasv_listen_fd) // nobody进程被动监听用户的连接，将监听端口发送给ftp服务进程; 在被动模式下使用
{
	int listen_fd = create_server_or_exit(config.listen_address.data(), 0);
	pasv_listen_fd = listen_fd;

	struct sockaddr_in local_addr{};
	get_local_addr(listen_fd, &local_addr);

	unsigned short port = ntohs(local_addr.sin_port);
	write_n(nobody_pic_fd, &port, sizeof(unsigned short));
}

void accept_in_pasv(int nobody_pic_fd, int pasv_listen_fd) // nobody进程被动接收客户端的连接，将得到的data fd发送给ftp服务进程
{
	int conn_fd = accept_with_timeout(pasv_listen_fd, NULL, config.accept_timeout);
	if (conn_fd == -1) { send_char(nobody_pic_fd, result_bad); return; }

	send_char(nobody_pic_fd, result_ok);
	send_fd(nobody_pic_fd, conn_fd);

	close(pasv_listen_fd);
	close(conn_fd);
}


void handle_port(Session& session) // PORT 192,168,0,1,123,456
{
	session.get_port_addr();

	write_n(session.control_fd, "200 PORT command successful. Consider using PASV.\r\n");
}
void handle_pasv(Session& session)
{
	send_char(session.pic_fd, LISTEN_IN_PASV); //  令nobody进程随机选取一个端口，进行监听

	unsigned short port;
	read_n(session.pic_fd, &port, sizeof(unsigned short)); // 从nobody进程获取监听的端口

	session.active_pasv = true;

	unsigned int vec[4];
	sscanf(config.listen_address.data(), "%u.%u.%u.%u", vec, vec+1, vec+2, vec+3);
	char text[1024] = {0};
	snprintf(text, sizeof(text), "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u).\r\n", vec[0], vec[1], vec[2], vec[3], port>>8, port&0xFF); // 向客户端发送ftp服务进程的监听ip地址和端口
	write_n(session.control_fd, text, strlen(text));
}
bool establish_transfer(Session& session)
{
	if (!session.active_pasv && !session.active_port) { write_n(session.control_fd, "425 Use PORT or PASV first.\r\n"); return false; }
	if (session.active_pasv && session.active_port) { fprintf(stderr, "服务器主动模式和被动模式均被启用! \n"); return false; }

	if (session.active_port)
	{
		send_char(session.pic_fd, CONNECT_IN_ACTV);

		send_buf(session.pic_fd, (char*)&session.port_addr, sizeof(session.port_addr)); // 也可以分别发送端口和字符串形式的IP地址。

		char res = get_char(session.pic_fd); // 接收nobody进程建立的数据连接fd
		if (res == 0) { fprintf(stderr, "nobody进程异常退出.\n"); exit(EXIT_FAILURE); }
		if (res == result_bad) { return false; }
		else if (res == result_ok) { session.data_fd = recv_fd(session.pic_fd); }
	}
	if (session.active_pasv)
	{
		send_char(session.pic_fd, ACCEPT_IN_PASV);
		char res = get_char(session.pic_fd);
		if (res == 0) { fprintf(stderr, "nobody进程异常退出.\n"); exit(EXIT_FAILURE); }
		if (res == result_bad) { return false; }
		else if (res == result_ok)
		{
			session.data_fd = recv_fd(session.pic_fd);
		}
	}

	return true;
}

void handle_retr(Session& session) // Retrieve, 客户下载文件
{
	if (!establish_transfer(session))
	{
		session.reset_data_fd();
		write_n(session.control_fd, "550 establish_transfer failed.\r\n");
		return;
	}

	int file_fd = open(session.arg.data(), O_RDONLY); // todo : RAII
	if (file_fd == -1) { perror("open"); write_n(session.control_fd, "550 Failed to open file.\r\n"); session.reset_data_fd(); return; }

	struct stat st_buf{};
	int fstat_ret = fstat(file_fd, &st_buf);
	if (fstat_ret == -1 || !S_ISREG(st_buf.st_mode)) { write_n(session.control_fd, "550 Failed to open file.\r\n"); close(file_fd); session.reset_data_fd(); return; }

	int lock_ret = lock_file_read(file_fd);
	if (lock_ret == -1) { write_n(session.control_fd, "550 Failed to open file.\r\n"); close(file_fd); session.reset_data_fd(); return; }

	long offset = session.restart_position; session.restart_position = 0; // 获取并重置断点续传位置
	long bytes_to_send = (offset > st_buf.st_size) ? 0 : (st_buf.st_size - offset); // 获取要发送的字节数
	if (offset > 0)
	{
		long lseek_ret = lseek(file_fd, offset, SEEK_SET); // 偏移文件指针
		if (lseek_ret == -1) { write_n(session.control_fd, "550 Failed to open file.\r\n"); close(file_fd); session.reset_data_fd(); return; }
	}

	char text[1024] = {0};
	session.ascii_mode ?
	snprintf(text, sizeof(text), "150 Opening ASCII mode data connection for %s (%ld bytes).\r\n",session.arg.data(), st_buf.st_size)
	:snprintf(text, sizeof(text), "150 Opening BINARY mode data connection for %s (%ld bytes).\r\n",session.arg.data(), st_buf.st_size);

	session.init_transfer_start_time_point(); // 获取当前时间

	bool err_occur {false};
	while (bytes_to_send != 0)
	{
		if (config.idle_data_timeout > 0) // 先判断阻塞IO是否超时
		{
			int ret = detect_write_timeout(session.data_fd, config.idle_data_timeout);
			if (ret == -1 && errno == ETIMEDOUT)
			{
				write_n(session_ptr->control_fd, "421 Data timeout. Please Reconnect.\r\n");
				exit(EXIT_SUCCESS);
			}
		}

		long bytes_will_send = bytes_to_send > 65536 ? 65536 : bytes_to_send; // todo : 可配置trunk的大小
		ssize_t sendfile_ret = sendfile(session.data_fd, file_fd, NULL, bytes_will_send);
		bytes_to_send -= bytes_will_send;
		if (sendfile_ret == -1) { err_occur = true; break; }


		session.limit_transmission_speed(sendfile_ret, false); // 限制传输速度

		if (session.received_abor) { err_occur = true; break; } // 收到abor命令后，终止传输
	}

	close(file_fd); // 关闭文件时，自动释放文件锁
	session.reset_data_fd();

	err_occur ?
	write_n(session.control_fd, "426 Failure writing to network stream.\r\n")
	:write_n(session.control_fd, "226 Transfer complete.\r\n");

	if (session.received_abor)
	{
		session.received_abor = false;
		write_n(session.control_fd, "226 ABOR successful.\r\n");
	}
}

void upload_auxiliary(Session& session, bool append_mode) // 用户上传文件
{
	if (!establish_transfer(session))
	{
		session.reset_data_fd();
		write_n(session.control_fd, "550 establish_transfer failed.\r\n");
		return;
	}

	int file_fd = open(session.arg.data(), O_CREAT | O_WRONLY, 0666);
	if (file_fd == -1) { write_n(session.control_fd, "553 Failed to create file.\r\n"); session.reset_data_fd(); return; }

	struct stat st_buf{};
	int fstat_ret = fstat(file_fd, &st_buf);
	if (fstat_ret == -1 || !S_ISREG(st_buf.st_mode)) { write_n(session.control_fd, "550 Failed to open file.\r\n"); close(file_fd); session.reset_data_fd(); return; }

	int lock_ret = lock_file_write(file_fd);
	if (lock_ret == -1) { write_n(session.control_fd, "553 Failed to create file.\r\n"); close(file_fd); session.reset_data_fd(); return; }

	long long offset = session.restart_position; session.restart_position = 0; // 获取并重置断点续传位置

	if (!append_mode && offset == 0)
	{
		int ftruncate_ret = ftruncate(file_fd, 0); // 清空文件
		long lseek_ret = lseek(file_fd, 0, SEEK_SET);
		if (ftruncate_ret == -1 || lseek_ret == -1) { write_n(session.control_fd, "553 Failed to create file.\r\n"); close(file_fd); session.reset_data_fd(); return;}
	}
	else if(!append_mode) // offset != 0
	{
		long lseek_ret = lseek(file_fd, offset, SEEK_SET);
		if (lseek_ret == -1) { write_n(session.control_fd, "553 Failed to create file.\r\n"); close(file_fd); session.reset_data_fd(); return; }
	}
	else // append_mode = false
	{
		long lseek_ret = lseek(file_fd, 0, SEEK_END);
		if (lseek_ret == -1) { write_n(session.control_fd, "553 Failed to create file.\r\n"); close(file_fd); session.reset_data_fd(); return; }
	}

	char text[1024] = {0};
	session.ascii_mode ?
	snprintf(text, sizeof(text), "150 Opening ASCII mode data connection for %s (%ld bytes).\r\n",session.arg.data(), st_buf.st_size)
	:snprintf(text, sizeof(text), "150 Opening BINARY mode data connection for %s (%ld bytes).\r\n",session.arg.data(), st_buf.st_size);
	write_n(session.control_fd, text, strlen(text));



	int err_flag {0};
	char buffer[65536]; // todo : 可配置trunk的大小。当前为64K
	session.init_transfer_start_time_point();
	while (true)
	{
		if (config.idle_data_timeout > 0) // 先判断阻塞IO是否超时
		{
			int ret = detect_read_timeout(session.data_fd, config.idle_data_timeout);
			if (ret == -1 && errno == ETIMEDOUT)
			{
				write_n(session_ptr->control_fd, "421 Data timeout. Please Reconnect.\r\n");
				exit(EXIT_SUCCESS);
			}
		}

		ssize_t ret = read(session.data_fd, buffer, sizeof(buffer));
		if (ret == -1 && errno == EINTR) {  continue; }
		else if (ret == -1) { err_flag = 1; break; }
		else if (ret == 0) { break; }



		if (write_n(file_fd, buffer, ret) != ret) { err_flag = 2; break; } // todo : mmap; 先判断阻塞IO是否超时, 相关函数已实现于block_socket头文件

		session.limit_transmission_speed(ret, true); // 限制传输速度

		if (session.received_abor) { err_flag = 1; break; } // 收到abor命令, 终止传输
	}

	close(file_fd); // todo : RAII
	session.reset_data_fd();

	if (err_flag == 0) { write_n(session.control_fd, "226 Transfer complete.\r\n"); }
	else if (err_flag == 1) { write_n(session.control_fd, "426 Failure reading from network stream.\r\n"); }
	else { write_n(session.control_fd, "451 Failure writing to local file.\r\n"); }

	if (session.received_abor)
	{
		session.received_abor = false;
		write_n(session.control_fd, "226 ABOR successful.\r\n");
	}
}
void handle_stor(Session& session) { upload_auxiliary(session, false); }
void handle_appe(Session& session) { upload_auxiliary(session, true); } // 服务器维护了一个偏移量，从偏移量处开始上传

void handle_user(Session& session) // 用户登录，输入用户名; 通过用户名获取uid
{
	if (session.uid) {  write_n(session.control_fd, "530 Login incorrect, you are already logged in.\r\n"); return; }
	struct passwd* pw = getpwnam(session.arg.data());
	if (pw == NULL) { write_n(session.control_fd, "530 Login incorrect.\r\n"); return; }
	write_n(session.control_fd, "331 Please specify the password.\r\n");
	session.uid = pw->pw_uid;
}
void handle_pass(Session& session) // 用户登录，输入密码; 通过uid获取影子文件的密文，对比用户设置用户的权限
{
	if (!session.uid) { write_n(session.control_fd, "530 Use USER first.\r\n"); return; }
	struct passwd* pw = getpwuid(session.uid.value());
	if (pw == NULL) { write_n(session.control_fd, "530 Login incorrect.\r\n"); return; }
	struct spwd* sp = getspnam(pw->pw_name); // 获取系统保存的密文密码
	if (sp == NULL) { write_n(session.control_fd, "530 Login incorrect.\r\n"); return; }
	char* encrypted = crypt(session.arg.data(), sp->sp_pwdp); // 加密用户输入的明文密码
	if (strcmp(encrypted, sp->sp_pwdp) != 0) { write_n(session.control_fd, "530 Login incorrect.\r\n"); return; } // 验证密码

	set_permissions(pw->pw_gid, pw->pw_uid, config.local_umask);

	activate_SIGURG(session.control_fd); // 接收到带外数据时，产生SIGURG信号
	signal(SIGURG, sigurg_handler);

	write_n(session.control_fd, "230 Login successful.\r\n");

	session.logged_in = true;
}



void handle_list(Session& session) // 详细列出当前目录下的文件
{
	if (!establish_transfer(session))
	{
		session.reset_data_fd();
		write_n(session.control_fd, "550 establish_transfer failed.\r\n");
		return;
	}

	write_n(session.control_fd, "150 Here comes the directory listing.\r\n");
	vector<string> list = list_dir();
	for (const string& str : list) { write_n(session.data_fd, str.data(), str.size()); fprintf(stderr, "%s", str.data()); }

	write_n(session.control_fd, "226 Directory send ok.\r\n");

	session.reset_data_fd();
}
void handle_nlst(Session& session) // 简要列出当前目录下的文件
{
	if (!establish_transfer(session))
	{
		session.reset_data_fd();
		write_n(session.control_fd, "550 establish_transfer failed.\r\n");
		return;
	}

	write_n(session.control_fd, "150 Here comes the directory listing.\r\n");
	vector<string> list = list_neat_dir();
	for (const string& str : list) { write_n(session.data_fd, str.data(), str.size()); }
	write_n(session.control_fd, "226 Directory send ok.\r\n");

	session.reset_data_fd();
}
void handle_size(Session& session) // 获取文件大小
{
	struct stat buf{};
	int ret = stat(session.arg.data(), &buf);

	if (ret == -1)
	{ write_n(session.control_fd, "550 SIZE operation failed.\r\n"); }
	else if (!S_ISREG(buf.st_mode))
	{ write_n(session.control_fd, "550 Could not get file size.\r\n"); }
	else
	{
		char text[1024] = {0};
		snprintf(text, sizeof(text), "213 %ld\r\n", buf.st_size);
		write_n(session.control_fd, text);
	}
}
void handle_pwd(Session& session) // 通过getcwd()获取当前工作目录（绝对路径）
{
	char dir[1024 + 1] = {0};
	if (getcwd(dir, 1024) == NULL) { fprintf(stderr, "getcwd failed \n"); }
	char text[1033] = {0};
	snprintf(text, sizeof(text), "257 \"%s\"\r\n", dir);
	write_n(session.control_fd, text, strlen(text));
}
void handle_cwd(Session& session) // 通过chdir()切换工作目录
{
	int ret = chdir(session.arg.data());

	if (ret == -1) { perror("chdir"); write_n(session.control_fd, "550 Failed to change directory.\r\n"); }
	else { write_n(session.control_fd, "250 Directory successfully changed.\r\n"); }
}
void handle_cdup(Session& session) // 切换到上级目录
{
	int ret = chdir("..");

	if (ret == -1) { perror("chdir"); write_n(session.control_fd, "550 Failed to change directory.\r\n"); }
	else { write_n(session.control_fd, "250 Directory successfully changed.\r\n"); }
}
void handle_mkd(Session& session) // 创建目录
{
	int ret = mkdir(session.arg.data(), 0777);

	if (ret == -1) { write_n(session.control_fd, "550 Create directory operation failed.\r\n"); }
	else
	{
		char path[4096] = {0};
		char text[4196] = {0};
		if ( getcwd(path, sizeof(path)) == NULL)
			{ perror("getcwd"); snprintf(text, sizeof(text), "257 %s created.\r\n", session.arg.data()); }
		else { snprintf(text, sizeof(text), "257 %s/%s created.\r\n", path, session.arg.data()); }
		write_n(session.control_fd, text);
	}

}
void handle_dele(Session& session) // 删除文件
{
	int ret = unlink(session.arg.data());

	if (ret == -1) { write_n(session.control_fd, "550 Delete operation failed.\r\n"); return; }
	write_n(session.control_fd, "250 Delete operation successful.\r\n");
}
void handle_rmd(Session& session) // 删除目录
{
	int ret = rmdir(session.arg.data());

	if (ret == -1) { write_n(session.control_fd, "550 Remove directory operation failed.\r\n"); }
	else { write_n(session.control_fd, "250 Remove directory operation successful.\r\n"); }
}
void handle_rnto(Session& session) // 重命名文件
{
	if (session.rnfr_name.empty()) { write_n(session.control_fd, "503 RNFR required first.\r\n"); return; }

	int ret = rename(session.rnfr_name.data(), session.arg.data());
	if (ret == -1) { perror("rename"); write_n(session.control_fd, "550 Rename failed.\r\n"); return; }

	string().swap(session.rnfr_name); assert(session.rnfr_name.empty());

	write_n(session.control_fd, "250 Rename successful.\r\n");
}



void handle_site(Session& session)
{
	vector<string> args = session.split_arg();
	if (args.back().back() == '\r') { args.back().pop_back(); } // 去除末尾的\r

	if (!args.empty() && args.front() == "CHMOD") // SITE CHMOD 0666 /home/kiki 	对挂载到虚拟机上的文件无效
	{
		if (args.size() != 3) { write_n(session.control_fd, "500 SITE CHMOD needs 2 arguments.\r\n"); return; }

		int ret = chmod(args[2].data(), std::stoi(args[1], nullptr, 8));
		ret != -1 ? write_n(session.control_fd, "200 SITE CHMOD command ok.\r\n") : write_n(session.control_fd, "500 SITE CHMOD command failed.\r\n");
	}
	else if (!args.empty() && args.front() == "UMASK") // SITE UMASK [umask]
	{
		if (args.size() == 1)
		{
			char text[1024] = {0};
			snprintf(text, sizeof(text), "200 Your current UMASK is 0%o.\r\n", config.local_umask);
			write_n(session.control_fd, text);
		}
		else
		{
			mode_t new_umask = std::stoi(args[1], nullptr, 8);
			umask(new_umask);
			char text[1024] = {0};
			snprintf(text, sizeof(text), "200 UMASK set to 0%o.\r\n", new_umask);
			config.local_umask = new_umask;
			write_n(session.control_fd, text);
		}
	}
	else if(!args.empty() && args.front() == "HELP")
	{
		write_n(session.control_fd, "214 CHMOD UMASK HELP.\r\n");
	}
	else { write_n(session.control_fd, "500 Unknown SITE command.\r\n"); }

}
void handle_stat(Session& session)
{
	write_n(session.control_fd, "211-FTP server status:\r\n");

	char text[1024] = {0};
	config.max_upload_rate == 0 ?
	snprintf(text, sizeof(text), "	No session upload bandwidth limit\r\n"):
	snprintf(text, sizeof(text), "	Session upload bandwidth limit is %u bytes/s\r\n", config.max_upload_rate);
	write_n(session.control_fd, text);
	config.max_download_rate == 0 ?
	snprintf(text, sizeof(text), "	No session download bandwidth limit\r\n"):
	snprintf(text, sizeof(text), "	Session download bandwidth limit is %u bytes/s\r\n", config.max_download_rate);
	write_n(session.control_fd, text);

	snprintf(text, sizeof(text), "	total connections limit is %u\r\n", config.maximum_clients);
	write_n(session.control_fd, text);
	snprintf(text, sizeof(text), "	max connections per ip is %u\r\n", config.max_conns_per_ip);
	write_n(session.control_fd, text);

	write_n(session.control_fd, "211 End of status.\r\n");
}


void handle_type(Session& session) // 设置数据的传输方式。二进制格式和文本格式的区别在于是否转换'\n'字符。本程序只支持二进制格式。
{
	if (session.arg == "A")
	{
		session.ascii_mode = true;
		write_n(session.control_fd, "200 Switching to ASCII mode.\r\n");
	}
	else if (session.arg == "I")
	{
		session.ascii_mode = false;
		write_n(session.control_fd, "200 Switching to Binary mode.\r\n");
	}
	else { write_n(session.control_fd, "500 Unrecognised TYPE command.\r\n"); }
}
void handle_rest(Session& session) // 设置断点续传偏移量
{
	session.restart_position = std::stol(session.arg);

	char buf[1024] = {0};
	snprintf(buf, sizeof(buf), "350 Restart position accepted (%zu).\r\n", session.restart_position);
	write_n(session.control_fd, buf);
}
void handle_rnfr(Session& session) // 获取要重命名的文件名
{
	session.rnfr_name = session.arg;

	write_n(session.control_fd, "350 Ready for RNTO.\r\n");
}
void handle_noop(Session& session) // no operation, 防止服务器断开空闲连接，不进行任何操作。
{
	write_n(session.control_fd, "200 NOOP ok.\r\n");
}
void handle_quit(Session& session)
{
	write_n(session.control_fd, "221 Goodbye.\r\n");
	exit(EXIT_SUCCESS);
}
void handle_abor(Session& session)
{
	write_n(session.control_fd, "225 No transfer to ABOR.\r\n");
}
void handle_help(Session& session)
{
	write_n(session.control_fd, "214 Help OK.\r\n");
}
void handle_syst(Session& session) // 获取ftp服务器上操作系统的类型。
{
	write_n(session.control_fd, "215 UNIX Type: L8\r\n");
}
void handle_feat(Session& session) // 获取服务器的特性
{
	write_n(session.control_fd, "211-Features:\r\n");
	write_n(session.control_fd, " PASV\r\n");
	write_n(session.control_fd, " REST STREAM\r\n"); // 支持断点续传
	write_n(session.control_fd, " SIZE\r\n");
	write_n(session.control_fd, " UTF8\r\n");
	write_n(session.control_fd, "211 End\r\n");
}

unordered_map<string, void(*)(Session& session)> services =
{
	{"USER", handle_user},
	{"PASS", handle_pass},
	{"SYST", handle_syst},
	{"FEAT", handle_feat}, // 获取服务器的特性
	{"PWD", handle_pwd},
	{"TYPE", handle_type},
	{"PORT", handle_port},
	{"LIST", handle_list},
	{"PASV", handle_pasv},
	{"NLST", handle_nlst},
	{"CWD", handle_cwd},
	{"CDUP", handle_cdup},
	{"DELE", handle_dele},
	{"REST", handle_rest},
	{"MKD", handle_mkd},
	{"RMD", handle_rmd},
	{"SIZE", handle_size},
	{"RNFR", handle_rnfr},
	{"RNTO", handle_rnto},
	{"RETR", handle_retr},
	{"STOR", handle_stor},
	{"APPE", handle_appe},
	{"NOOP", handle_noop},
	{"QUIT", handle_quit},
	{"ABOR", handle_abor},
	{"HELP", handle_help},
	{"STAT", handle_stat},
	{"SITE", handle_site},
	{"STRU", NULL}, // 获取文件结构
	{"MODE", NULL}, // 传输模式
	{"MDTM", NULL}, // 在文件传输后保存文件的原始日期和时间信息,而不是传输时的时间
	{"EPRT", NULL}, // 设置网络协议
	{"EPSV", NULL} // 设置网络协议
};
