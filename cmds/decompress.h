#pragma once
#include "command_base.h"
#include "../utils/compress/decompress.h"
#include <filesystem>

class DecompressCommand : public Command {
public:
    const char* name() const override { 
        return "decompress"; 
    }
    
    const char* description() const override { 
        return "Decompress a .cfmp archive"; 
    }
    
    const char* usage() const override { 
        return "decompress <archive_file> [output]"; 
    }
    
    int minArgs() const override { 
        return 1; 
    }
    
    int run(CommandContext& ctx) override {
        std::string file = ctx.args[0];

        if (std::filesystem::is_directory(file)) {
            Logger::Log(LOG_ERROR, "Specified path is a folder: " + file);
            return 1;
        }

        std::string output = ctx.args.size() >= 2 ? ctx.args[1] : 
                             std::filesystem::path(file).filename().stem().string();
        
        // Build full output path in save_path
        std::filesystem::path unpackedFilePath = std::filesystem::path(ctx.config.save_path) / output;
        
        Logger::Log(LOG_INFO, "Decompressing file: " + file);
        Logger::Log(LOG_INFO, "Output path: " + unpackedFilePath.string());
        Logger::StartTimer("Decompressing archive");
        
        if (!decompressFile(file, unpackedFilePath.string())) {
            Logger::Log(LOG_ERROR, "Failed to pack folder");
            return 1;
        }
        
        Logger::EndTimer("Decompressing archive", LOG_INFO);
        Logger::Log(LOG_INFO, "Archive decompressed successfully");
        return 0;
    }
};

COMMAND(DecompressCommand)