// mini_ftpd
// Created by kiki on 2022/2/22.9:31
// 如果文件是符号链接文件，则lstat获取该文件本身的状态。
#include <dirent.h> // opendir
#include <sys/stat.h> // stat
#include <unistd.h> // readlink
#include <sys/time.h> // gettimeofday
#include <time.h> // localtime, strftime
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include "file_utility.h"

vector<string> list_neat_dir(const char* path)
{
	vector<string> list;

	DIR* dir = opendir(path);
	if (dir == NULL) { return list; }

	struct dirent* dt;
	while ( (dt = readdir(dir)) != NULL)
	{
		char absolute_path[2048] = {0};
		snprintf(absolute_path, sizeof(absolute_path), "%s/%s", path, dt->d_name);

		struct stat s{};
		if ( lstat(absolute_path, &s) == -1 ) { perror("lstat"); continue; }

		char buf[2048] = {0};
		if (S_ISLNK(s.st_mode))
		{
			char src_path[1024] = {0};
			ssize_t ret = readlink(absolute_path, src_path, sizeof (src_path));
			if (ret == -1) { perror("readlink"); }
			snprintf(buf, sizeof(buf), "%s -> %s\r\n", dt->d_name, src_path);
		}
		else { snprintf(buf, sizeof(buf), "%s\r\n", dt->d_name); }

		list.emplace_back(buf);
	}

	closedir(dir);

	return list;
}
#include <iostream>
using namespace std;

vector<string> list_dir(const char* path)
{
	vector<string> list;
	list.emplace_back("文件类型    链接数 uid     gid          大小    日期       文件名\r\n");

	DIR* dir = opendir(path);
	if (dir == NULL) { return list; }

	struct dirent* dt;
	while ( (dt = readdir(dir)) != NULL)
	{
		char absolute_path[2048] = {0};
		snprintf(absolute_path, sizeof(absolute_path), "%s/%s", path, dt->d_name);

		struct stat s{};
		if ( lstat(absolute_path, &s) == -1 ) { perror("lstat"); continue; }
		// 获取文件类型
		char perm[] = "----------";
		mode_t mode = s.st_mode & S_IFMT;
		switch (mode)
		{
			case S_IFREG: perm[0] = '-'; break;
			case S_IFDIR: perm[0] = 'd'; break;
			case S_IFLNK: perm[0] = 'l'; break;
			case S_IFIFO: perm[0] = 'p'; break;
			case S_IFSOCK: perm[0] = 's'; break;
			case S_IFCHR: perm[0] = 'c'; break;
			case S_IFBLK: perm[0] = 'b'; break;
			default: perm[0] = '?'; break;
		}
		// 获取文件权限
		if (s.st_mode & S_IRUSR) { perm[1] = 'r'; }
		if (s.st_mode & S_IWUSR) { perm[2] = 'w'; }
		if (s.st_mode & S_IXUSR) { perm[3] = 'x'; }
		if (s.st_mode & S_IRGRP) { perm[4] = 'r'; }
		if (s.st_mode & S_IWGRP) { perm[5] = 'w'; }
		if (s.st_mode & S_IXGRP) { perm[6] = 'x'; }
		if (s.st_mode & S_IROTH) { perm[7] = 'r'; }
		if (s.st_mode & S_IWOTH) { perm[8] = 'w'; }
		if (s.st_mode & S_IXOTH) { perm[9] = 'x'; }
		if (s.st_mode & S_ISUID) { perm[3] = perm[3] == 'x' ? 's' : 'S'; } // S表示可执行文件访问其它文件的权限，由可执行文件的拥有者决定，而不是执行可执行文件的用户。
		if (s.st_mode & S_ISGID) { perm[6] = perm[6] == 'x' ? 's' : 'S'; }
		if (s.st_mode & S_ISVTX) { perm[9] = perm[9] == 'x' ? 't' : 'T'; } // t表示目录只能被它的拥有者删除
		// 获取文件的链接数、uid、gid、大小
		int offset = 0;
		char buf[2048] = {0};
		offset += snprintf(buf, sizeof(buf), "%s ", perm);
		offset += snprintf(buf + offset, sizeof(buf), "%3lu %-8d %-8d ", s.st_nlink, s.st_uid, s.st_gid);
		offset += snprintf(buf + offset, sizeof(buf), "%8lu ", s.st_size);
		// 获取文件的最后修改日期。半年前修改的文件将有特殊的日期格式。
		struct timeval tv{};
		gettimeofday(&tv, NULL);
		const char* format = (s.st_mtime > tv.tv_sec || tv.tv_sec - s.st_mtime > 365/2*24*60*60) ? "%b %e %H:%M" : "%b %e  %Y";
		struct tm* tm_ptr = localtime(&tv.tv_sec);
		char date[64] = {0};
		strftime(date, sizeof(date), format, tm_ptr);
		offset += snprintf(buf + offset, sizeof(buf), "%s ", date);
        // 如果是符号链接文件，获取链接指向文件的文件名
		if (S_ISLNK(s.st_mode))
		{
			char src_path[1024] = {0};
			ssize_t ret = readlink(absolute_path, src_path, sizeof (src_path));
			if (ret == -1) { perror("readlink"); }
			snprintf(buf + offset, sizeof(buf), "%s -> %s\r\n", dt->d_name, src_path);
		}
		else { snprintf(buf + offset, sizeof(buf), "%s\r\n", dt->d_name); }

		list.emplace_back(buf);
	}

	closedir(dir);

	return list;
}

int lock_file_read(int fd)
{
	struct flock lock{};
	lock.l_type = F_RDLCK;
	lock.l_whence = SEEK_SET; // 从文件头部开始加锁
	lock.l_start = 0; // 从l_whence偏移0字节开始加锁
	lock.l_len = 0; // 将整个文件加锁

	int ret;
	do // 防止信号打断
	{
		ret = fcntl(fd, F_SETLKW, &lock); // F_SETLKW 阻塞等待其它进程释放加到文件上的锁
	}
	while (ret == -1 && errno == EINTR);

	return ret;
}
int lock_file_write(int fd)
{
	struct flock lock{};
	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;

	int ret;
	do
	{
		ret = fcntl(fd, F_SETLKW, &lock);
	}
	while (ret == -1 && errno == EINTR);

	return ret;
}
int unlock_file(int fd)
{
	struct flock lock{};
	lock.l_type = F_ULOCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;

	return fcntl(fd, F_SETLK, &lock); // 解锁时不阻塞等待
}