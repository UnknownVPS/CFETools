#pragma once
#include "command_base.h"
#include "../func/folder_packer/folder_packer.h"
#include <filesystem>

extern std::string save_path;

class UnpackCommand : public Command {
public:
    const char* name() const override { 
        return "unpack"; 
    }
    
    const char* description() const override { 
        return "Unpack .cfup archive"; 
    }
    
    const char* usage() const override { 
        return "unpack <archive.cfup> [output_dir]"; 
    }
    
    int minArgs() const override { 
        return 1; 
    }
    
    int run(CommandContext& ctx) override {
        std::string archive = ctx.args[0];
        
        if (!std::filesystem::exists(archive)) {
            Logger::Log(LOG_ERROR, "Archive file does not exist: " + archive);
            return 1;
        }
        
        // Default output folder in save_path with archive stem name
        std::string output_dir = ctx.args.size() >= 2 ? ctx.args[1] :
                                 std::filesystem::path(archive).stem().string();
        std::filesystem::path unpackedFolder = std::filesystem::path(save_path) / output_dir;
        
        Logger::Log(LOG_INFO, "Unpacking archive: " + archive);
        Logger::Log(LOG_INFO, "Output folder: " + unpackedFolder.string());
        Logger::StartTimer("Folder unpacking");
        
        if (!unpack_packed_file(archive, unpackedFolder.string())) {
            Logger::Log(LOG_ERROR, "Failed to unpack archive");
            return 1;
        }
        
        Logger::EndTimer("Folder unpacking", LOG_INFO);
        Logger::Log(LOG_INFO, "Extracted successfully");
        return 0;
    }
};

COMMAND(UnpackCommand)