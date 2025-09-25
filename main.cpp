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
#include "utils/hashers/fileHasher.hpp"
#include "version.h"
#include "globals.h"
#include <filesystem>

bool no_encrypt = false;
bool twofile_system = false; 
bool grayscale = false;
bool isCompressed = false;
bool isPacked = false;
bool disableHash = false;
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
    Logger::StartTimer("Total Execution");
    const char* home;

    // Parse the args
    ArgParser parser(argc, argv);
    parser.hasFlag("debug", "d") ? Logger::SetLevel(LOG_DEBUG) : Logger::SetLevel(LOG_INFO);
    no_encrypt = parser.hasFlag("no-encrypt", "ne");
    twofile_system = parser.hasFlag("two-file", "2f");
    grayscale = parser.hasFlag("grayscale", "gs");
    if (parser.hasFlag("version", "v")) {
        Logger::Log(LOG_INFO, "CFET-Tools version: " VERSION);
        Logger::Log(LOG_INFO, "Author: unknownpersonog");
        return 0;
    }
    disableHash = parser.hasFlag("skip-hash", "nh");
    if (parser.hasFlag("sha256", "sha")) {
        std::string path = parser.getValue("sha256", "sha", "");
        if (path.empty()) {
            Logger::Log(LOG_ERROR, "No file specified for hashing.");
            return 1;
        }
        try {
            std::string hash = fileHasher::hashFileSHA256(path);
            Logger::Log(LOG_INFO, "SHA-256 Hash: " + hash);
        } catch (const std::exception& e) {
            Logger::Log(LOG_ERROR, std::string("Error hashing file: ") + e.what());
            return 1;
        }
        return 0;
    }
    if (parser.hasFlag("crc32", "crc")) {
        std::string path = parser.getValue("crc32", "crc", "");
        if (path.empty()) {
            Logger::Log(LOG_ERROR, "No file specified for CRC32 calculation.");
            return 1;
        }
        try {
            uint32_t crc = fileHasher::crc32_file(path);
            std::ostringstream oss;
            oss << std::hex << std::uppercase << crc;
            Logger::Log(LOG_INFO, "CRC32: " + oss.str());
        } catch (const std::exception& e) {
            Logger::Log(LOG_ERROR, std::string("Error calculating CRC32: ") + e.what());
            return 1;
        }
        return 0;
    }
    std::string compress_arg = parser.getValue("compress", "c", "");
    std::string img_input = parser.getValue("img", "i", "");
    std::string file_input = parser.getValue("file", "f", "");
    Logger::Log(LOG_INFO, std::string("Following modes are enabled: ") + (twofile_system ? "2 Filesystem (Discontinued) " : "") + (grayscale ? "8-bit " : "1-bit ") + (no_encrypt ? "Unencrypted " : "Encrypted "));
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
        Logger::StartTimer("Processing image to file");
        create_file(img_input);
        Logger::EndTimer("Processing image to file", LOG_INFO);
    }

    if (!file_input.empty()) {
        Logger::Log(LOG_INFO, "Processing the input file at: " + file_input);
        std::filesystem::path inputPath(file_input);
        std::string path_to_encode;

        if (std::filesystem::is_directory(inputPath)) {
            std::string packedFileName = inputPath.filename().string() + ".cfup";
            std::filesystem::path packedFilePath = std::filesystem::path(save_path) / packedFileName;

            Logger::Log(LOG_INFO, "Input is a folder. Packing it into: " + packedFilePath.string());
            Logger::StartTimer("Folder packing");
            if (!pack_folder(file_input, packedFilePath.string())) {
                Logger::Log(LOG_ERROR, "Failed to pack folder.");
                return 1;
            }
            Logger::EndTimer("Folder packing", LOG_INFO);
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
            Logger::StartTimer("File compression");
            compressFile(path_to_encode, (std::filesystem::path(save_path) / compressedFilename).string(), compress_level);
            Logger::EndTimer("File compression", LOG_INFO);
            if (std::filesystem::is_directory(inputPath)) {
                Logger::Log(LOG_INFO, "Removing temporary packed folder: " + path_to_encode);
                std::filesystem::remove_all(path_to_encode);
            }
            isCompressed = true;
            path_to_encode = (std::filesystem::path(save_path) / compressedFilename).string();
        }
        Logger::StartTimer("Encoding file to image");
        create_img(path_to_encode);
        Logger::EndTimer("Encoding file to image", LOG_INFO);
        if (std::filesystem::is_directory(inputPath) || !compress_arg.empty()) {
            std::filesystem::remove(path_to_encode);
            Logger::Log(LOG_DEBUG, "Removed temporary file: " + path_to_encode);
        }
    }
    Logger::EndTimer("Total Execution", LOG_INFO);
    return 0;
}