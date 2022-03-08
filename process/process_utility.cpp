// mini_ftpd
// Created by kiki on 2022/2/23.16:46
#include <signal.h>
#include <unistd.h> // alarm
#include <pwd.h> // getpwnam
#include <sys/capability.h> // 该头文件需要安装sudo apt-get install libcap-dev
#include <stdlib.h>
#include <sys/stat.h> // umask
#include <stdio.h>
#include <time.h>
#include <errno.h>

/* sleep()和alarm()可能都采用SIGALRM实现，产生冲突，这里使用nanosleep */
void sleep_for_seconds(double seconds)
{
	if (seconds <= 0) { return; }

	struct timespec ts;
	ts.tv_sec = (time_t)seconds;
	ts.tv_nsec = (time_t)((seconds - (double)ts.tv_sec) * 1000000000);

	while (nanosleep(&ts, &ts) == -1 && errno == EINTR) { }
}

void create_nobody_process()
{
	struct passwd* pw = getpwnam("nobody");
	if (pw == NULL) { perror("getpwnam"); exit(EXIT_FAILURE); }
	int ret = setegid(pw->pw_gid); // 设置当前进程的有效gid。如果先设置了用户id，可能没有权限再去修改用户id了。
	if (ret == -1) { perror("setegid"); exit(EXIT_FAILURE); }
	ret = seteuid(pw->pw_uid); // 设置当前进程的有效uid
	if (ret == -1) { perror("seteuid"); exit(EXIT_FAILURE); }

	struct __user_cap_header_struct cap_header; // 赋予nobody进程调用bind()的权限。
	cap_header.version = _LINUX_CAPABILITY_VERSION_3;
	cap_header.pid = 0;
	struct __user_cap_data_struct cap_data;
	__u32 cap_mask = 0;
	cap_mask |= (1 << CAP_NET_BIND_SERVICE);
	cap_data.effective = cap_mask;
	cap_data.permitted = cap_mask;
	cap_data.inheritable = 0; // 子进程不继承特权
	ret = capset(&cap_header, &cap_data);
	if (ret == -1) { perror("capset"); fprintf(stderr, "%d", errno);exit(EXIT_FAILURE); }
}

void set_permissions(gid_t egid, uid_t euid, mode_t mask)
{
	int ret = setegid(egid);
	if (ret == -1) { perror("setegid"); exit(EXIT_FAILURE); }
	ret = seteuid(euid);
	if (ret == -1) { perror("seteuid"); exit( EXIT_FAILURE); }

	umask(mask); // 这个系统调用总会成功
}