#include "progressbar.h"

void print_progress(float progress, const std::chrono::steady_clock::time_point& start_time, int total_operations, const string& task_name) {
    int bar_width = 70;

    auto current_time = std::chrono::steady_clock::now();
    auto time_elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count();

    float speed = 0;
    int eta = 0;

    if(time_elapsed > 0) {
        speed = total_operations * progress / (float)time_elapsed;
        eta = (total_operations - total_operations * progress) / speed;
    }

    cout << "\033[2K\r"; // Clear the line
    cout << "(" << task_name << ") ";
    cout << "[";
    int pos = bar_width * progress;
    for (int i = 0; i < bar_width; ++i) {
        if (i < pos) cout << "=";
        else if (i == pos) cout << ">";
        else cout << " ";
    }
    int display_progress = int(progress * 100.0 + 1);
    cout << "] " << display_progress << "% | ";
    cout << "Elapsed time: " << time_elapsed << " s | ";
    cout << "Speed: " << speed << " ops/s | ";
    cout << "ETA: " << eta << " s";
    cout.flush();
}