#pragma once
#include "command_base.h"
#include "../utils/hashers/fileHasher.hpp"
#include <filesystem>
#include "../utils/hashers/folderHasher.hpp"

class HashCommand : public Command {
public:
    const char* name() const override { 
        return "hash"; 
    }
    
    const char* description() const override { 
        return "Calculate file hashes"; 
    }
    
    const char* usage() const override { 
        return "hash <file> [--sha] [--crc]"; 
    }
    
    int minArgs() const override { 
        return 1; 
    }
    
    int run(CommandContext& ctx) override {
        std::string file = ctx.args[0];

        if (!std::filesystem::exists(file)) {
            Logger::Log(LOG_ERROR, "File does not exist: " + file);
            return 1;
        }
        if (std::filesystem::is_directory(file)) {
            Logger::Log(LOG_INFO, "Folder detected, using custom hash function (not crypto secure)");
            std::string folderHash = FolderHasher::hash_folder(file, !ctx.config.noRecursionFlag);
            Logger::Log(LOG_INFO, "Folder Hash: " + folderHash);
            if (ctx.config.exportInfoFlag) {
                // Extract just the folder name, not the full path
                std::string folder_name = std::filesystem::path(file).filename().string();
                if (folder_name.empty()) {
                    // Handle case where path ends with / (filename() returns empty)
                    folder_name = std::filesystem::path(file).parent_path().filename().string();
                }
                
                auto output_path = std::filesystem::path(ctx.config.save_path) / (folder_name + "_hash_info.txt");
                FolderHasher::export_folder_info(file, output_path.string(), !ctx.config.noRecursionFlag);
                Logger::Log(LOG_INFO, "Exported folder hash info to " + output_path.string());
            }
            return 0;
        }
        bool anyHash = false;
        
        if (ctx.config.shaEnabled) {
            std::string hash = fileHasher::hashFileSHA256(file);
            Logger::Log(LOG_INFO, "SHA-256: " + hash);
            anyHash = true;
        }
        if (ctx.config.crcEnabled) {
            std::string crc = fileHasher::crc32_file(file);
            Logger::Log(LOG_INFO, "CRC32: " + crc);
            anyHash = true;
        }
        
        // Default to xxHash if no specific hash requested
        if (!anyHash) {
            std::string hash = fileHasher::xxhash_file(file);
            Logger::Log(LOG_INFO, "xxHash64: " + hash);
        }
        
        return 0;
    }
};

COMMAND(HashCommand)