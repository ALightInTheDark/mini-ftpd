// Test
// Created by kiki on 2022/2/25.16:46
#include <file_utility.h>
#include <iostream>
#include <vector>
#include <string>
using namespace std;

int main()
{
	vector<string> list = list_neat_dir();
	for (const auto& str : list) { cout << str << endl; }

	cout << endl;

	list = list_dir();
	for (const auto& str : list) { cout << str << endl; }
}