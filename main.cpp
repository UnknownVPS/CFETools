#include "utils/logger/logger.h" 
#include "func/img_creation/create_img.h"
#include "func/file_creation/create_file.h"
#include "utils/userinput/user_input.h"
bool isDebugMode = false;

int main(int argc, char* argv[]) {
    std::string input;
    std::string file_path;
    std::string save_path;
    const char* home;
    Logger::SetLevel(LOG_INFO);
    bool file_flag = false;
    bool img_flag = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--debug") {
            isDebugMode = true;
            Logger::SetLevel(LOG_DEBUG);
            Logger::Log(LOG_DEBUG, "Running in DEBUG mode.");
        } else if (arg.substr(0, 6) == "--img=") {
            if (file_flag) {
                Logger::Log(LOG_ERROR, "Both --file and --img options provided.");
                return 1;
            }
            img_flag = true;
            file_path = arg.substr(6);
            input = "img";
        } else if (arg.substr(0, 7) == "--file=") {
            if (img_flag) {
                Logger::Log(LOG_ERROR, "Both --file and --img options provided.");
                return 1;
            }
            file_flag = true;
            file_path = arg.substr(7);
            input = "file";
        }
    }
    Logger::Log(LOG_DEBUG, "Checking directory status");
    #ifdef _WIN32
        home = std::getenv("USERPROFILE");
        Logger::Log(LOG_DEBUG, "Detected kernel: Windows");
    #else
        home = std::getenv("HOME");
        Logger::Log(LOG_DEBUG, "Detected kernel: Linux");
    #endif

    if (home != nullptr) {
        std::filesystem::path tool_dir(home);
        tool_dir /= "CFET-Tools"; // Append "CFET-Tools" to the path

        if (!std::filesystem::exists(tool_dir)) {
            Logger::Log(LOG_INFO, "Directory does not exist. Creating..");
            std::filesystem::create_directory(tool_dir);
        }
        save_path = tool_dir.string();
        Logger::Log(LOG_DEBUG, "Currently using directory at " + save_path);
    } else {
        Logger::Log(LOG_ERROR, "Cannot get home directory.");
        return 1;
    }
    Input inputprompt;
    if (input.empty()) {
        Logger::Log(LOG_DEBUG, "User input argument was empty.");
        input = inputprompt.ask("Please select a command to proceed (img/file): ");
    }

    if (input == "img") {
        if (file_path.empty()) {
            file_path = inputprompt.ask("Please enter the file to be encoded path: ");
            if (!filesystem::exists(file_path)) {
                Logger::Log(LOG_ERROR, "File path is invalid. Please recheck.");
                return 404;
            }
        }
        create_img(file_path, save_path);
    } else if (input == "file") {
        if (file_path.empty()) {
            file_path = inputprompt.ask("Please enter the image to be decoded path: ");
            if (!filesystem::exists(file_path)) {
                Logger::Log(LOG_ERROR, "Image path is invalid. Please recheck.");
                return 404;
            }
        }
        create_file(file_path, save_path);
    } else {
        Logger::Log(LOG_ERROR, "Invalid input was received. Exiting");
    }

    return 0;
}
