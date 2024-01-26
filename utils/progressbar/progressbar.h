#include <iostream>
#include <chrono>
#include <string>

using namespace std;

void print_progress(float progress, const std::chrono::steady_clock::time_point& start_time, int total_operations, const string& task_name);
