#include <iostream>
#include <string>
#include <vector>
#include <optional>
#include <set>
#include <map>
#include "utils/logger/logger.h"
#include "func/img_creation/create_img.h"
#include "func/file_creation/create_file.h"
#include "func/folder_packer/folder_packer.h"
#include "utils/userinput/user_input.h"
#include "utils/compress/compress.h"
#include "utils/hashers/fileHasher.hpp"
#include "func/patch_creation/patch.h"
#include "version.h"
#include "globals.h"
#include <filesystem>

// Global configuration flags
bool no_encrypt = false;
bool twofile_system = false; 
bool grayscale = false;
bool isCompressed = false;
bool isPacked = false;
bool disableHash = false;
std::string save_path = "";
bool shaEnabled = false;
bool crcEnabled = false;

enum class Operation {
    NONE,
    ENCODE,
    DECODE,
    PACK,
    UNPACK,
    DIFF,
    PATCH,
    HASH,
    HELP,
    VERSIONC
};

class ArgParser {
private:
    std::vector<std::string> args;
    static const std::map<std::string, Operation> operationMap;

public:
    ArgParser(int argc, char* argv[]) {
        for (int i = 1; i < argc; i++) {
            args.push_back(std::string(argv[i]));
        }
    }

    bool hasFlag(const std::string& longArg, const std::string& shortArg = "") {
        for (const auto& arg : args) {
            if (arg == "--" + longArg || 
                (!shortArg.empty() && arg == "-" + shortArg)) {
                return true;
            }
        }
        return false;
    }

    std::optional<std::string> getValue(const std::string& longArg, const std::string& shortArg = "") {
        for (size_t i = 0; i < args.size(); i++) {
            bool isLongMatch = args[i] == "--" + longArg;
            bool isShortMatch = !shortArg.empty() && args[i] == "-" + shortArg;
            
            if (isLongMatch || isShortMatch) {
                if (i + 1 < args.size()) {
                    std::string nextArg = args[i + 1];
                    bool isNextArgFlag = (nextArg.substr(0, 2) == "--") || 
                                        (nextArg.size() > 1 && nextArg[0] == '-');
                    if (!isNextArgFlag) {
                        return args[i + 1];
                    }
                }
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::string getValue(const std::string& longArg, const std::string& shortArg, const std::string& defaultValue) {
        auto result = getValue(longArg, shortArg);
        return result.has_value() ? result.value() : defaultValue;
    }

    std::vector<std::string> getPositionalArgs() {
        std::vector<std::string> positional;
        for (size_t i = 0; i < args.size(); i++) {
            if (args[i][0] != '-') {
                positional.push_back(args[i]);
            } else if (args[i][0] == '-') {
                // Skip the next arg if this is a flag with value
                if (i + 1 < args.size() && args[i + 1][0] != '-') {
                    i++; // Skip the value
                }
            }
        }
        return positional;
    }

    Operation getOperation() {
        if (!args.empty() && args[0][0] != '-') {
            auto it = operationMap.find(args[0]);
            if (it != operationMap.end()) {
                return it->second;
            }
        }
        return Operation::NONE;
    }
};

const std::map<std::string, Operation> ArgParser::operationMap = {
    {"encode", Operation::ENCODE},
    {"decode", Operation::DECODE},
    {"pack", Operation::PACK},
    {"unpack", Operation::UNPACK},
    {"diff", Operation::DIFF},
    {"patch", Operation::PATCH},
    {"hash", Operation::HASH},
    {"help", Operation::HELP},
    {"version", Operation::VERSIONC}
};

void parseConfig(const std::string& cfg_value) {
    if (cfg_value.empty()) return;
    
    std::vector<std::string> cfg_params;
    size_t start = 0, end = 0;
    while ((end = cfg_value.find(',', start)) != std::string::npos) {
        std::string param = cfg_value.substr(start, end - start);
        if (!param.empty()) cfg_params.push_back(param);
        start = end + 1;
    }
    std::string last = cfg_value.substr(start);
    if (!last.empty()) cfg_params.push_back(last);

    Logger::Log(LOG_DEBUG, "Parsing configuration options:");
    for (const auto& param : cfg_params) {
        Logger::Log(LOG_DEBUG, "  - " + param);
        
        if (param == "sha" || param == "sha256") {
            shaEnabled = true;
        } else if (param == "crc" || param == "crc32") {
            crcEnabled = true;
        } else if (param == "no-encrypt" || param == "ne") {
            no_encrypt = true;
        } else if (param == "two-file" || param == "2f") {
            twofile_system = true;
        } else if (param == "grayscale" || param == "gs") {
            grayscale = true;
        } else if (param == "skip-hash" || param == "nh") {
            disableHash = true;
        } else if (param.rfind("compress=", 0) == 0 || param.rfind("c=", 0) == 0) {
            size_t eq_pos = param.find('=');
            if (eq_pos != std::string::npos) {
                std::string level = param.substr(eq_pos + 1);
                Logger::Log(LOG_DEBUG, "Compression level set to: " + level);
            }
        } else {
            Logger::Log(LOG_WARNING, "Unknown config parameter: " + param);
        }
    }
}

void showHelp() {
    Logger::Log(LOG_INFO, "CFET-Tools - File Encryption & Binary Diff Tool");
    Logger::Log(LOG_INFO, "");
    Logger::Log(LOG_INFO, "OPERATIONS:");
    Logger::Log(LOG_INFO, "  encode <file/folder>           Encode file/folder to image");
    Logger::Log(LOG_INFO, "  decode <image>                 Decode image to file");
    Logger::Log(LOG_INFO, "  pack <folder> <output>         Pack folder into .cfup archive");
    Logger::Log(LOG_INFO, "  unpack <archive> <output_dir>  Unpack .cfup archive");
    Logger::Log(LOG_INFO, "  diff <src> <dst> <patch>       Create binary diff patch");
    Logger::Log(LOG_INFO, "  patch <src> <patch> <output>   Apply binary patch");
    Logger::Log(LOG_INFO, "  hash <file>                    Calculate file hashes");
    Logger::Log(LOG_INFO, "");
    Logger::Log(LOG_INFO, "OPTIONS:");
    Logger::Log(LOG_INFO, "  -d, --debug                    Enable debug logging");
    Logger::Log(LOG_INFO, "  -v, --version                  Show version information");
    Logger::Log(LOG_INFO, "  -h, --help                     Show this help message");
    Logger::Log(LOG_INFO, "  -cfg, --config <options>       Configuration options (comma-separated)");
    Logger::Log(LOG_INFO, "");
    Logger::Log(LOG_INFO, "CONFIG OPTIONS (use with -cfg):");
    Logger::Log(LOG_INFO, "  sha, sha256                    Enable SHA-256 hashing");
    Logger::Log(LOG_INFO, "  crc, crc32                     Enable CRC32 hashing");
    Logger::Log(LOG_INFO, "  no-encrypt, ne                 Disable encryption");
    Logger::Log(LOG_INFO, "  two-file, 2f                   Use two-file system");
    Logger::Log(LOG_INFO, "  grayscale, gs                  Use grayscale mode (8-bit)");
    Logger::Log(LOG_INFO, "  skip-hash, nh                  Skip hash verification");
    Logger::Log(LOG_INFO, "  compress=<level>, c=<level>    Compression level (1-21)");
}

int handleEncode(ArgParser& parser, const std::vector<std::string>& positional) {
    if (positional.size() < 2) {
        Logger::Log(LOG_ERROR, "No input file/folder specified for encode operation");
        Logger::Log(LOG_INFO, "Usage: cfet-tools encode <file/folder>");
        return 1;
    }
    
    std::string file_input = positional[1];
    Logger::Log(LOG_INFO, "Encoding file/folder: " + file_input);
    std::filesystem::path inputPath(file_input);
    std::string path_to_encode;

    // Handle folder: pack it first
    if (std::filesystem::is_directory(inputPath)) {
        std::string packedFileName = inputPath.filename().string() + ".cfup";
        std::filesystem::path packedFilePath = std::filesystem::path(save_path) / packedFileName;

        Logger::Log(LOG_INFO, "Packing folder to: " + packedFilePath.string());
        Logger::StartTimer("Folder packing");
        // pack_folder takes FULL PATHS (input folder and output archive path)
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
        Logger::Log(LOG_ERROR, "Invalid input path.");
        return 1;
    }

    // Handle compression
    std::string compress_arg = parser.getValue("compress", "c", "");
    if (!compress_arg.empty()) {
        std::filesystem::path encodePath(path_to_encode);
        std::string compressedFilename = encodePath.filename().string() + ".cfmp";
        std::filesystem::path compressedFilePath = std::filesystem::path(save_path) / compressedFilename;
        int compress_level = std::stoi(compress_arg);
        
        Logger::StartTimer("File compression");
        // compressFile takes FULL PATHS (input and output file paths)
        if (!compressFile(path_to_encode, compressedFilePath.string(), compress_level)) {
            Logger::Log(LOG_ERROR, "Failed to compress file.");
            return 1;
        }
        Logger::EndTimer("File compression", LOG_INFO);
        
        // Clean up intermediate packed file
        if (isPacked) {
            std::filesystem::remove(path_to_encode);
        }
        isCompressed = true;
        path_to_encode = compressedFilePath.string();
    }

    // Encode to image
    Logger::StartTimer("Encoding to image");
    // create_img takes a file path and uses save_path for output internally
    create_img(path_to_encode);
    Logger::EndTimer("Encoding to image", LOG_INFO);
    
    // Clean up temporary files
    if (isPacked || isCompressed) {
        std::filesystem::remove(path_to_encode);
        Logger::Log(LOG_DEBUG, "Cleaned up temporary file");
    }
    return 0;
}

int handleDecode(const std::vector<std::string>& positional) {
    if (positional.size() < 2) {
        Logger::Log(LOG_ERROR, "No image specified for decode operation");
        Logger::Log(LOG_INFO, "Usage: cfet-tools decode <image>");
        return 1;
    }
    
    std::string img_input = positional[1];
    Logger::Log(LOG_INFO, "Decoding image: " + img_input);
    if (!std::filesystem::exists(img_input)) {
        Logger::Log(LOG_ERROR, "Image file does not exist.");
        return 1;
    }
    Logger::StartTimer("Decoding image");
    // createFile takes image path and uses save_path for output internally
    createFile(img_input);
    Logger::EndTimer("Decoding image", LOG_INFO);
    return 0;
}

int handlePack(const std::vector<std::string>& positional) {
    if (positional.size() < 2) {
        Logger::Log(LOG_ERROR, "No folder specified for pack operation");
        Logger::Log(LOG_INFO, "Usage: cfet-tools pack <folder> <output.cfup>");
        return 1;
    }
    
    std::string folder = positional[1];
    std::string output = positional.size() >= 3 ? positional[2] : 
                         std::filesystem::path(folder).filename().string() + ".cfup";
    
    if (!std::filesystem::is_directory(folder)) {
        Logger::Log(LOG_ERROR, "Specified path is not a folder.");
        return 1;
    }
    
    // Build full output path in save_path
    std::filesystem::path packedFilePath = std::filesystem::path(save_path) / output;
    
    Logger::Log(LOG_INFO, "Packing folder: " + folder);
    Logger::Log(LOG_INFO, "Output archive: " + packedFilePath.string());
    Logger::StartTimer("Folder packing");
    // pack_folder takes FULL PATHS
    if (!pack_folder(folder, packedFilePath.string())) {
        Logger::Log(LOG_ERROR, "Failed to pack folder.");
        return 1;
    }
    Logger::EndTimer("Folder packing", LOG_INFO);
    Logger::Log(LOG_INFO, "Archive created successfully");
    return 0;
}

int handleUnpack(const std::vector<std::string>& positional) {
    if (positional.size() < 2) {
        Logger::Log(LOG_ERROR, "No archive specified for unpack operation");
        Logger::Log(LOG_INFO, "Usage: cfet-tools unpack <archive.cfup> <output_dir>");
        return 1;
    }
    
    std::string archive = positional[1];
    if (!std::filesystem::exists(archive)) {
        Logger::Log(LOG_ERROR, "Archive file does not exist.");
        return 1;
    }
    
    // Default output folder in save_path with archive stem name
    std::string output_dir = positional.size() >= 3 ? positional[2] :
                             std::filesystem::path(archive).stem().string();
    std::filesystem::path unpackedFolder = std::filesystem::path(save_path) / output_dir;
    
    Logger::Log(LOG_INFO, "Unpacking archive: " + archive);
    Logger::Log(LOG_INFO, "Output folder: " + unpackedFolder.string());
    Logger::StartTimer("Folder unpacking");
    // unpack_packed_file takes FULL PATHS
    if (!unpack_packed_file(archive, unpackedFolder.string())) {
        Logger::Log(LOG_ERROR, "Failed to unpack archive.");
        return 1;
    }
    Logger::EndTimer("Folder unpacking", LOG_INFO);
    Logger::Log(LOG_INFO, "Extracted successfully");
    return 0;
}

int handleDiff(const std::vector<std::string>& positional) {
    if (positional.size() < 4) {
        Logger::Log(LOG_ERROR, "Missing arguments for diff operation");
        Logger::Log(LOG_INFO, "Usage: cfet-tools diff <source> <destination> <patch_file>");
        return 1;
    }

    std::string src   = positional[1];
    std::string dst   = positional[2];
    std::string patch = positional[3];

    if (!std::filesystem::exists(src)) {
        Logger::Log(LOG_ERROR, "Source file does not exist: " + src);
        return 1;
    }
    if (!std::filesystem::exists(dst)) {
        Logger::Log(LOG_ERROR, "Destination file does not exist: " + dst);
        return 1;
    }

    // Build full patch path in save_path
    std::filesystem::path patchPath = std::filesystem::path(save_path) / patch;

    Logger::Log(LOG_INFO, "Creating diff patch...");
    Logger::Log(LOG_DEBUG, "Source: " + src);
    Logger::Log(LOG_DEBUG, "Destination: " + dst);
    Logger::Log(LOG_DEBUG, "Patch: " + patchPath.string());
    
    Logger::StartTimer("Diff generation");
    // createPatch takes FULL PATHS
    fastcdc::createPatch(src.c_str(), dst.c_str(), patchPath.string().c_str());
    Logger::EndTimer("Diff generation", LOG_INFO);
    Logger::Log(LOG_INFO, "Patch created: " + patchPath.string());
    return 0;
}

int handlePatch(const std::vector<std::string>& positional) {
    if (positional.size() < 4) {
        Logger::Log(LOG_ERROR, "Missing arguments for patch operation");
        Logger::Log(LOG_INFO, "Usage: cfet-tools patch <source> <patch_file> <output>");
        return 1;
    }

    std::string src    = positional[1];
    std::string patch  = positional[2];
    std::string output = positional[3];

    if (!std::filesystem::exists(src)) {
        Logger::Log(LOG_ERROR, "Source file does not exist: " + src);
        return 1;
    }
    if (!std::filesystem::exists(patch)) {
        Logger::Log(LOG_ERROR, "Patch file does not exist: " + patch);
        return 1;
    }

    // Build full output path in save_path
    std::filesystem::path outputPath = std::filesystem::path(save_path) / output;

    Logger::Log(LOG_INFO, "Applying patch...");
    Logger::Log(LOG_DEBUG, "Source: " + src);
    Logger::Log(LOG_DEBUG, "Patch: " + patch);
    Logger::Log(LOG_DEBUG, "Output: " + outputPath.string());
    
    Logger::StartTimer("Patch application");
    // applyPatch takes FULL PATHS
    fastcdc::applyPatch(src.c_str(), patch.c_str(), outputPath.string().c_str());
    Logger::EndTimer("Patch application", LOG_INFO);
    Logger::Log(LOG_INFO, "Patched file created: " + outputPath.string());
    return 0;
}

int handleHash(const std::vector<std::string>& positional) {
    if (positional.size() < 2) {
        Logger::Log(LOG_ERROR, "No file specified for hash operation");
        Logger::Log(LOG_INFO, "Usage: cfet-tools hash <file> -cfg sha,crc");
        return 1;
    }
    
    std::string file = positional[1];
    
    if (!std::filesystem::exists(file)) {
        Logger::Log(LOG_ERROR, "File does not exist: " + file);
        return 1;
    }
    
    bool anyHash = false;
    if (shaEnabled) {
        std::string hash = fileHasher::hashFileSHA256(file);
        Logger::Log(LOG_INFO, "SHA-256: " + hash);
        anyHash = true;
    }
    if (crcEnabled) {
        std::string crc = fileHasher::crc32_file(file);
        Logger::Log(LOG_INFO, "CRC32: " + crc);
        anyHash = true;
    }
    
    if (!anyHash) {
        std::string hash = fileHasher::xxhash_file(file);
        Logger::Log(LOG_INFO, "xxHash64: " + hash);
    }
    return 0;
}

int main(int argc, char* argv[]) {
    Logger::StartTimer("Total Execution");

    ArgParser parser(argc, argv);

    // Handle help and version flags
    if (parser.hasFlag("help", "h") || argc == 1) {
        showHelp();
        return 0;
    }

    if (parser.hasFlag("version", "v")) {
        Logger::Log(LOG_INFO, "CFET-Tools version: " VERSION);
        Logger::Log(LOG_INFO, "Author: unknownpersonog");
        return 0;
    }

    // Set debug level
    if (parser.hasFlag("debug", "d")) {
        Logger::SetLevel(LOG_DEBUG);
    } else {
        Logger::SetLevel(LOG_INFO);
    }

    // Parse configuration
    std::string cfg_value = parser.getValue("config", "cfg", "");
    if (!cfg_value.empty()) {
        parseConfig(cfg_value);
    }

    // Log active modes
    Logger::Log(LOG_INFO, std::string("Active modes: ") + 
                (twofile_system ? "2-File " : "") + 
                (grayscale ? "8-bit " : "1-bit ") + 
                (no_encrypt ? "Unencrypted " : "Encrypted "));

    // Set up save path
    Logger::Log(LOG_DEBUG, "Checking system type");
    const char* home;
    #ifdef _WIN32
        Logger::Log(LOG_DEBUG, "Detected system: Windows");
        home = std::getenv("USERPROFILE");
    #else
        Logger::Log(LOG_DEBUG, "Detected system: Unix-like");
        home = std::getenv("HOME");
    #endif

    if (home != nullptr) {
        std::filesystem::path tool_dir(home);
        tool_dir /= "CFET-Tools";
        if (!std::filesystem::exists(tool_dir)) {
            Logger::Log(LOG_INFO, "Creating CFET-Tools directory...");
            std::filesystem::create_directory(tool_dir);
        }
        save_path = tool_dir.string();
        Logger::Log(LOG_DEBUG, "Working directory: " + save_path);
    } else {
        Logger::Log(LOG_ERROR, "Cannot determine home directory.");
        return 1;
    }

    // Get operation and execute
    Operation operation = parser.getOperation();
    std::vector<std::string> positional = parser.getPositionalArgs();

    int result = 0;
    try {
        switch (operation) {
            case Operation::ENCODE:
                result = handleEncode(parser, positional);
                break;
                
            case Operation::DECODE:
                result = handleDecode(positional);
                break;
                
            case Operation::PACK:
                result = handlePack(positional);
                break;
                
            case Operation::UNPACK:
                result = handleUnpack(positional);
                break;
                
            case Operation::DIFF:
                result = handleDiff(positional);
                break;
                
            case Operation::PATCH:
                result = handlePatch(positional);
                break;
                
            case Operation::HASH:
                result = handleHash(positional);
                break;
                
            case Operation::HELP:
                showHelp();
                break;
                
            case Operation::VERSIONC:
                Logger::Log(LOG_INFO, "CFET-Tools version: " VERSION);
                Logger::Log(LOG_INFO, "Author: unknownpersonog");
                break;
                
            case Operation::NONE:
            default:
                Logger::Log(LOG_ERROR, "Unknown or missing operation");
                Logger::Log(LOG_INFO, "Use --help for list of available operations");
                result = 1;
                break;
        }
    } catch (const std::exception& e) {
        Logger::Log(LOG_ERROR, std::string("Error: ") + e.what());
        result = 1;
    }

    Logger::EndTimer("Total Execution", LOG_INFO);
    return result;
}