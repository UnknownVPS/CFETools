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

int main(int argc, char* argv[]) {
    const char* home;

    // Parse the args
    ArgParser parser(argc, argv);
    parser.hasFlag("debug", "d") ? Logger::SetLevel(LOG_DEBUG) : Logger::SetLevel(LOG_INFO);
    Logger::Log(LOG_DEBUG, "Debug mode enabled ");
    no_encrypt = parser.hasFlag("no-encrypt", "ne");
    Logger::Log(LOG_DEBUG, "Encryption: " + std::to_string(!no_encrypt));
    aio = parser.hasFlag("aio", "a");
    Logger::Log(LOG_DEBUG, "AIO mode: " + std::to_string(aio));
    grayscale = parser.hasFlag("grayscale", "gs");
    Logger::Log(LOG_DEBUG, "Grayscale mode: " + std::to_string(grayscale));
    if (parser.hasFlag("version", "v")) {
        Logger::Log(LOG_INFO, "CFET-Tools version: " VERSION);
        Logger::Log(LOG_INFO, "Author: unknownpersonog");
        return 0;
    }
    std::string compress_arg = parser.getValue("compress", "c", "");
    std::string img_input = parser.getValue("img", "i", "");
    std::string file_input = parser.getValue("file", "f", "");
    Logger::Log(LOG_INFO, std::string("Following modes are enabled: ") + (aio ? "AIO " : "") + (grayscale ? "8-bit " : "1-bit ") + (no_encrypt ? "Unencrypted " : "Encrypted "));
    // System checks
    Logger::Log(LOG_DEBUG, "Checking system type");
    #ifdef _WIN32
        Logger::Log(LOG_DEBUG, "Detected system: Windows");
        home = std::getenv("USERPROFILE");
    #else
        Logger::Log(LOG_DEBUG, "Detected system: Unix-like");
        home = std::getenv("HOME");
    #endif

    // Directory checks
    Logger::Log(LOG_DEBUG, "Checking CFET-Tools directory status... ");
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

    // Handlers
    if (img_input.empty() && file_input.empty()) {
        Logger::Log(LOG_INFO, "img: to convert a decrypt a file");
        Logger::Log(LOG_INFO, "file: to encrypt a file");
        Input prompt;
        std::string input = prompt.ask("What would you like to do?: ");
        if ((input == "img" || input == "i")) { img_input = prompt.ask("Path to image to be decrypted: "); }
        else if ((input == "file" || input == "f")) { file_input = prompt.ask("Path to file to be encrypted: "); }
        else {Logger::Log(LOG_ERROR, "Invalid option. Exiting. ");}
    }
    if (!img_input.empty() && !file_input.empty()) {
        Logger::Log(LOG_ERROR, "Cannot specify both -i/--img and -f/--file");
        return 2;
    }

    if (!img_input.empty()) {
        Logger::Log(LOG_INFO, "Processing the input image at: " + img_input);
        if (!std::filesystem::exists(img_input)) {
            Logger::Log(LOG_ERROR, "Invalid input. Path is invalid. Please recheck.");
            return 2;
        }
        create_file(img_input);
    }

    if (!file_input.empty()) {
        Logger::Log(LOG_INFO, "Processing the input file at: " + file_input);
        std::filesystem::path inputPath(file_input);
        std::string path_to_encode;

        if (std::filesystem::is_directory(inputPath)) {
            std::string packedFileName = inputPath.filename().string() + ".cfup";
            std::filesystem::path packedFilePath = std::filesystem::path(save_path) / packedFileName;

            Logger::Log(LOG_INFO, "Input is a folder. Packing it into: " + packedFilePath.string());

            if (!pack_folder_toc(file_input, packedFilePath.string())) {
                Logger::Log(LOG_ERROR, "Failed to pack folder.");
                return 1;
            }
            isPacked = true;
            path_to_encode = packedFilePath.string();
        } else if (std::filesystem::is_regular_file(inputPath)) {
            path_to_encode = file_input;
        } else {
            Logger::Log(LOG_ERROR, "Invalid input. Input path is not a file or folder.");
            return 2;
        }
        if (!compress_arg.empty()) {
            std::filesystem::path encodePath(path_to_encode);
            std::string compressedFilename = encodePath.filename().string() + ".cfmp";
            int compress_level = std::stoi(compress_arg);
            Logger::Log(LOG_DEBUG, "Compressing");
            compressFile(path_to_encode, (std::filesystem::path(save_path) / compressedFilename).string(), compress_level);
            if (std::filesystem::is_directory(inputPath)) {
                Logger::Log(LOG_INFO, "Removing temporary packed folder: " + path_to_encode);
                std::filesystem::remove_all(path_to_encode);
            }
            isCompressed = true;
            path_to_encode = (std::filesystem::path(save_path) / compressedFilename).string();
        }
        create_img(path_to_encode);

        if (std::filesystem::is_directory(inputPath) || !compress_arg.empty()) {
            std::filesystem::remove(path_to_encode);
            Logger::Log(LOG_DEBUG, "Removed temporary file: " + path_to_encode);
        }
    }
}