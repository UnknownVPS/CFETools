#pragma once
#include "command_base.h"
#include "../func/folder_packer/folder_packer.h"
#include <filesystem>

extern std::string save_path;

class PackCommand : public Command {
public:
    const char* name() const override { 
        return "pack"; 
    }
    
    const char* description() const override { 
        return "Pack folder into .cfup archive"; 
    }
    
    const char* usage() const override { 
        return "pack <folder> [output.cfup]"; 
    }
    
    int minArgs() const override { 
        return 1; 
    }
    
    int run(CommandContext& ctx) override {
        std::string folder = ctx.args[0];
        
        if (!std::filesystem::is_directory(folder)) {
            Logger::Log(LOG_ERROR, "Specified path is not a folder: " + folder);
            return 1;
        }
        
        std::string output = ctx.args.size() >= 2 ? ctx.args[1] : 
                             std::filesystem::path(folder).filename().string() + ".cfup";
        
        // Build full output path in save_path
        std::filesystem::path packedFilePath = std::filesystem::path(save_path) / output;
        
        Logger::Log(LOG_INFO, "Packing folder: " + folder);
        Logger::Log(LOG_INFO, "Output archive: " + packedFilePath.string());
        Logger::StartTimer("Folder packing");
        
        if (!pack_folder(folder, packedFilePath.string())) {
            Logger::Log(LOG_ERROR, "Failed to pack folder");
            return 1;
        }
        
        Logger::EndTimer("Folder packing", LOG_INFO);
        Logger::Log(LOG_INFO, "Archive created successfully");
        return 0;
    }
};

COMMAND(PackCommand)