#include <iostream>
#include <chrono>
#include <string>
#include <thread>
#include <atomic>

using namespace std;

class ProgressBar {
public:
    ProgressBar(atomic<float>& progress_var);
    ~ProgressBar();

private:
    void run();
    void print() const;

    atomic<float>& progress;
    float last_progress;
    atomic<bool> running;
    thread progress_thread;
};
