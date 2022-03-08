// mini_ftpd
// Created by kiki on 2022/2/23.16:46
#pragma once

extern void sleep_for_seconds(double seconds);
extern void create_nobody_process();
extern void set_permissions(gid_t egid, uid_t euid, mode_t mask);