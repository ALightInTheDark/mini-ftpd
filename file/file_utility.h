// mini_ftpd
// Created by kiki on 2022/2/22.9:34
#pragma once
#include <vector>
#include <string>
using std:: vector, std::string;

extern vector<string> list_neat_dir(const char* path = ".");
extern vector<string> list_dir(const char* path = ".");

extern int lock_file_read(int fd); // todo : RAII
extern int lock_file_write(int fd);
extern int unlock_file(int fd);
