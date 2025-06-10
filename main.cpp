#include "utils/logger/logger.h"
#include "func/img_creation/create_img.h"
#include "func/file_creation/create_file.h"
#include "func/folder_packer/folder_packer.h"
#include "utils/userinput/user_input.h"
#include "version.h"
#include <filesystem>
#include <cstdlib>
bool isDebugMode = false;

int main(int argc, char* argv[]) {
    std::string input;
    std::string file_path;
    std::string save_path;
    const char* home;
    Logger::SetLevel(LOG_INFO);
    bool file_flag = false;
    bool img_flag = false;
    bool no_encrypt = false;
    bool aio_mode = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--debug") {
            isDebugMode = true;
            Logger::SetLevel(LOG_DEBUG);
            Logger::Log(LOG_DEBUG, "Running in DEBUG mode.");
            Logger::Log(LOG_DEBUG, "Using version: " VERSION);
        } else if (arg == "--no-encrypt") {
            no_encrypt = true;
            Logger::Log(LOG_INFO, "Encryption will be skipped.");
        } else if (arg.substr(0, 6) == "--img=") {
            if (file_flag) {
                Logger::Log(LOG_ERROR, "Cannot use --img and --file together.");
                return 1;
            }
            img_flag = true;
            file_path = arg.substr(6);
            input = "img";
        } else if (arg.substr(0, 7) == "--file=") {
            if (img_flag) {
                Logger::Log(LOG_ERROR, "Cannot use --img and --file together.");
                return 1;
            }
            file_flag = true;
            file_path = arg.substr(7);
            input = "file";
        } 
        else if (arg == "--version") {
            Logger::Log(LOG_INFO, "CFET-Tools version: " VERSION);
            return 0;
        } else if (arg == "--aio") {
            aio_mode = true;
            Logger::Log(LOG_INFO, "AIO mode enabled.");
        } else {
            Logger::Log(LOG_ERROR, "Unknown argument: " + arg);
            return 1;
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
        tool_dir /= "CFET-Tools";
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
            file_path = inputprompt.ask("Please enter the file or folder to be encoded path: ");
            if (!std::filesystem::exists(file_path)) {
                Logger::Log(LOG_ERROR, "Path is invalid. Please recheck.");
                return 404;
            }
        }

        std::filesystem::path inputPath(file_path);
        std::string path_to_encode;

        if (std::filesystem::is_directory(inputPath)) {
            std::string packedFileName = inputPath.filename().string() + ".cfup";
            std::filesystem::path packedFilePath = inputPath.parent_path() / packedFileName;

            Logger::Log(LOG_INFO, "Input is a folder. Packing it into: " + packedFilePath.string());

            if (!pack_folder(file_path, packedFilePath.string())) {
                Logger::Log(LOG_ERROR, "Failed to pack folder.");
                return 1;
            }

            path_to_encode = packedFilePath.string();
        } else if (std::filesystem::is_regular_file(inputPath)) {
            path_to_encode = file_path;
        } else {
            Logger::Log(LOG_ERROR, "Input path is not a file or folder.");
            return 1;
        }

        create_img(path_to_encode, save_path, no_encrypt, aio_mode);

        if (std::filesystem::is_directory(inputPath)) {
            std::filesystem::remove(path_to_encode);
            Logger::Log(LOG_DEBUG, "Removed temporary packed file: " + path_to_encode);
        }
    } else if (input == "file") {
        if (file_path.empty()) {
            file_path = inputprompt.ask("Please enter the BMP file to decode: ");
            if (!std::filesystem::exists(file_path)) {
                Logger::Log(LOG_ERROR, "Path is invalid. Please recheck.");
                return 404;
            }
        }
        create_file(file_path, save_path, aio_mode);
    } else {
        Logger::Log(LOG_ERROR, "Invalid input was received. Exiting");
    }

    return 0;
}
