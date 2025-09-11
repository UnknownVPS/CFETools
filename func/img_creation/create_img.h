#include <iostream>
#include <filesystem>
#include <cmath>
#include <cstdint>
#include <random>
#include <iomanip>
#include <sstream>

using namespace std;

void create_img(const std::string& file_path, const std::string& save_path, bool no_encrypt = false, bool aio_mode = false, bool grayscaleMode = false);
