#pragma once
#include "command_base.h"
#include "../func/file_creation/create_file.h"
#include <filesystem>

class DecodeCommand : public Command {
public:
    const char* name() const override { 
        return "decode"; 
    }
    
    const char* description() const override { 
        return "Decode image to file"; 
    }
    
    const char* usage() const override { 
        return "decode <image>"; 
    }
    
    int minArgs() const override { 
        return 1; 
    }
    
    int run(CommandContext& ctx) override {
        std::string img_input = ctx.args[0];
        
        if (!std::filesystem::exists(img_input)) {
            Logger::Log(LOG_ERROR, "Image file does not exist: " + img_input);
            return 1;
        }
        
        Logger::Log(LOG_INFO, "Decoding image: " + img_input);
        Logger::StartTimer("Decoding image");
        
        createFile(img_input);
        
        Logger::EndTimer("Decoding image", LOG_INFO);
        return 0;
    }
};

COMMAND(DecodeCommand)