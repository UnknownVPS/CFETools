#include <iostream>
#include <string>
#include <vector>
#include <optional>
#include "utils/logger/logger.h"
#include "func/img_creation/create_img.h"
#include "func/file_creation/create_file.h"
#include "func/folder_packer/folder_packer.h"
#include "utils/userinput/user_input.h"
#include "utils/compress/compress.h"
#include "version.h"
#include "globals.h"
#include <filesystem>

bool no_encrypt = false;
bool aio = false; 
bool grayscale = false;
bool isCompressed = false;
bool isPacked = false;
std::string save_path = "";

class ArgParser {
private:
    std::vector<std::string> args;

public:
    ArgParser(int argc, char* argv[]) {
        for (int i = 1; i < argc; i++) {
            args.push_back(std::string(argv[i]));
        }
    }

    // Check if a boolean flag exists
    bool hasFlag(const std::string& longArg, const std::string& shortArg) {
        for (const auto& arg : args) {
            if (arg == "--" + longArg || 
                (arg == "-" + shortArg)) {
                return true;
            }
        }
        return false;
    }

    // Get value for an argument that takes a parameter
    std::optional<std::string> getValue(const std::string& longArg, const std::string& shortArg) {
        for (size_t i = 0; i < args.size(); i++) {
            bool isLongMatch = args[i] == "--" + longArg;
            bool isShortMatch = args[i] == "-" + shortArg;
            
            if (isLongMatch || isShortMatch) {
                // Check if there's a next argument (the value)
                if (i + 1 < args.size()) {
                    std::string nextArg = args[i + 1];
                    
                    // Check if next argument is NOT a flag
                    bool isNextArgFlag = (nextArg.substr(0, 2) == "--") || 
                                    (nextArg[0] == '-');
                    
                    if (!isNextArgFlag) {
                        return args[i + 1];
                    }
                }
                return std::nullopt; // Argument found but no value
            }
        }
        return std::nullopt; // Argument not found
    }

    // Get value with default fallback
    std::string getValue(const std::string& longArg, const std::string& shortArg, const std::string& defaultValue) {
        auto result = getValue(longArg, shortArg);
        return result.has_value() ? result.value() : defaultValue;
    }
};

#include <chrono>  // for timing

// Utility function to log duration
template <typename Func>
auto logDuration(const std::string& label, Func&& func) {
    auto start = std::chrono::steady_clock::now();
    auto result = func();  // call the function
    auto end = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    Logger::Log(LOG_INFO, label + " completed in " + std::to_string(ms) + " ms");
    return result; // return the function’s result if any
}

int main(int argc, char* argv[]) {
    const char* home;
    ArgParser parser(argc, argv);

    parser.hasFlag("debug", "d") ? Logger::SetLevel(LOG_DEBUG) : Logger::SetLevel(LOG_INFO);
    Logger::Log(LOG_DEBUG, "Debug mode enabled ");
    no_encrypt = parser.hasFlag("no-encrypt", "ne");
    aio = parser.hasFlag("aio", "a");
    grayscale = parser.hasFlag("grayscale", "gs");

    if (parser.hasFlag("version", "v")) {
        Logger::Log(LOG_INFO, "CFET-Tools version: " VERSION);
        Logger::Log(LOG_INFO, "Author: unknownpersonog");
        return 0;
    }

    std::string compress_arg = parser.getValue("compress", "c", "");
    std::string img_input = parser.getValue("img", "i", "");
    std::string file_input = parser.getValue("file", "f", "");

    // System checks
    #ifdef _WIN32
        home = std::getenv("USERPROFILE");
    #else
        home = std::getenv("HOME");
    #endif

    if (home != nullptr) {
        std::filesystem::path tool_dir(home);
        tool_dir /= "CFET-Tools";
        if (!std::filesystem::exists(tool_dir)) {
            std::filesystem::create_directory(tool_dir);
        }
        save_path = tool_dir.string();
    } else {
        Logger::Log(LOG_ERROR, "Cannot get home directory.");
        return 1;
    }

    if (img_input.empty() && file_input.empty()) {
        Input prompt;
        std::string input = prompt.ask("What would you like to do?: ");
        if ((input == "img" || input == "i")) { img_input = prompt.ask("Path to image to be decrypted: "); }
        else if ((input == "file" || input == "f")) { file_input = prompt.ask("Path to file to be encrypted: "); }
        else { Logger::Log(LOG_ERROR, "Invalid option. Exiting."); }
    }

    if (!img_input.empty() && !file_input.empty()) {
        Logger::Log(LOG_ERROR, "Cannot specify both -i/--img and -f/--file");
        return 2;
    }

    if (!img_input.empty()) {
        Logger::Log(LOG_INFO, "Processing the input image at: " + img_input);
        if (!std::filesystem::exists(img_input)) {
            Logger::Log(LOG_ERROR, "Invalid input. Path is invalid.");
            return 2;
        }
        logDuration("Image extraction", [&]() {
            create_file(img_input);
            return 0;
        });
    }

    if (!file_input.empty()) {
        Logger::Log(LOG_INFO, "Processing the input file at: " + file_input);
        std::filesystem::path inputPath(file_input);
        std::string path_to_encode;

        if (std::filesystem::is_directory(inputPath)) {
            std::string packedFileName = inputPath.filename().string() + ".cfup";
            std::filesystem::path packedFilePath = std::filesystem::path(save_path) / packedFileName;
            Logger::Log(LOG_INFO, "Input is a folder. Packing it into: " + packedFilePath.string());

            bool success = logDuration("Folder packing", [&]() {
                return pack_folder(file_input, packedFilePath.string());
            });

            if (!success) {
                Logger::Log(LOG_ERROR, "Failed to pack folder.");
                return 1;
            }
            isPacked = true;
            path_to_encode = packedFilePath.string();
        } else if (std::filesystem::is_regular_file(inputPath)) {
            path_to_encode = file_input;
        } else {
            Logger::Log(LOG_ERROR, "Invalid input. Not a file or folder.");
            return 2;
        }

        if (!compress_arg.empty()) {
            std::filesystem::path encodePath(path_to_encode);
            std::string compressedFilename = encodePath.filename().string() + ".cfmp";
            int compress_level = std::stoi(compress_arg);

            logDuration("Compression", [&]() {
                compressFile(path_to_encode, (std::filesystem::path(save_path) / compressedFilename).string(), compress_level);
                return 0;
            });

            if (std::filesystem::is_directory(inputPath)) {
                std::filesystem::remove_all(path_to_encode);
            }
            isCompressed = true;
            path_to_encode = (std::filesystem::path(save_path) / compressedFilename).string();
        }

        logDuration("Image creation", [&]() {
            create_img(path_to_encode);
            return 0;
        });

        if (std::filesystem::is_directory(inputPath) || !compress_arg.empty()) {
            std::filesystem::remove(path_to_encode);
        }
    }
}