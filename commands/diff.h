#pragma once
#include "command_base.h"
#include "../func/patch_creation/patch.h"
#include <filesystem>

extern std::string save_path;

class DiffCommand : public Command {
public:
    const char* name() const override { 
        return "diff"; 
    }
    
    const char* description() const override { 
        return "Create binary diff patch"; 
    }
    
    const char* usage() const override { 
        return "diff <source> <destination> <patch_file>"; 
    }
    
    int minArgs() const override { 
        return 3; 
    }
    
    int run(CommandContext& ctx) override {
        std::string src   = ctx.args[0];
        std::string dst   = ctx.args[1];
        std::string patch = ctx.args[2];
        
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
        fastcdc::createPatch(src.c_str(), dst.c_str(), patchPath.string().c_str());
        Logger::EndTimer("Diff generation", LOG_INFO);
        
        Logger::Log(LOG_INFO, "Patch created: " + patchPath.string());
        return 0;
    }
};

COMMAND(DiffCommand)