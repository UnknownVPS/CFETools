#pragma once
#include "command_base.h"
#include "../func/patch_creation/patch.h"
#include <filesystem>

extern std::string save_path;

class PatchCommand : public Command {
public:
    const char* name() const override { 
        return "patch"; 
    }
    
    const char* description() const override { 
        return "Apply binary patch"; 
    }
    
    const char* usage() const override { 
        return "patch <source> <patch_file> <output>"; 
    }
    
    int minArgs() const override { 
        return 3; 
    }
    
    int run(CommandContext& ctx) override {
        std::string src    = ctx.args[0];
        std::string patch  = ctx.args[1];
        std::string output = ctx.args[2];
        
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
        fastcdc::applyPatch(src.c_str(), patch.c_str(), outputPath.string().c_str());
        Logger::EndTimer("Patch application", LOG_INFO);
        
        Logger::Log(LOG_INFO, "Patched file created: " + outputPath.string());
        return 0;
    }
};
COMMAND(PatchCommand)