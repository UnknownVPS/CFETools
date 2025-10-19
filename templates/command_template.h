#pragma once
#include "command_base.h"

class __CLASSNAME__Command : public Command {
public:
    const char* name() const override { 
        return "__NAME__"; 
    }
    
    const char* description() const override { 
        return "__DESCRIPTION__"; 
    }
    
    const char* usage() const override { 
        return "__NAME__ __USAGE_ARGS__"; 
    }
    
    int minArgs() const override { 
        return 1; // 1 = __MIN_ARGS__; 
    }
    
    int run(CommandContext& ctx) override {
        Logger::Log(LOG_INFO, "Running __NAME__ command");
        
        // Implement command logic here
        
        return 0;
    }
};

COMMAND(__CLASSNAME__Command)
