#pragma once
#include "command_base.h"
#include "../func/folder_packer/folder_packer.h"
#include "../func/folder_packer/ztc.hpp"
#include <filesystem>

extern std::string save_path;

class ZtcCommand : public Command {
public:
    const char* name() const override { 
        return "ztc"; 
    }
    
    const char* description() const override { 
        return "Convert .zip into .cfup archive"; 
    }
    
    const char* usage() const override { 
        return "ztc <zip_file> [output.cfup]"; 
    }
    
    int minArgs() const override { 
        return 1; 
    }
    
    int run(CommandContext& ctx) override {
        std::string file = ctx.args[0];
        
        if (!std::filesystem::is_regular_file(file)) {
            Logger::Log(LOG_ERROR, "Specified path is not a valid file: " + file);
            return 1;
        }
        
        std::string output = ctx.args.size() >= 2 ? ctx.args[1] : 
                             std::filesystem::path(file).filename().stem().string() + ".cfup";
        
        // Build full output path in save_path
        std::filesystem::path packedFilePath = std::filesystem::path(save_path) / output;
        
        Logger::Log(LOG_INFO, "Converting zip: " + file);
        Logger::Log(LOG_INFO, "Output archive: " + packedFilePath.string());
        Logger::StartTimer("Zip conversion");
        
        if (!convert_zip_to_cfup(file, packedFilePath.string())) {
            Logger::Log(LOG_ERROR, "Failed to convert zip to cfup");
            return 1;
        }
        
        Logger::EndTimer("Zip conversion", LOG_INFO);
        Logger::Log(LOG_INFO, "Archive created successfully");
        return 0;
    }
};

COMMAND(ZtcCommand)