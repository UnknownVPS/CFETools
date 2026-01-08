#pragma once
#include "command_base.h"
#include "../func/server/server.h"
#include <iostream>

class ServerCommand : public Command {
public:
    const char* name() const override { 
        return "server"; 
    }
    
    const char* description() const override { 
        return "Start a static file server"; 
    }
    
    const char* usage() const override { 
        return "server [folder] [--port <port>]"; 
    }
    
    int minArgs() const override { 
        return 0; 
    }
    
    int run(CommandContext& ctx) override {
        std::string folder = ".";
        if (!ctx.args.empty()) {
            folder = ctx.args[0];
        } else if (ctx.flags.count("folder")) {
            folder = ctx.flags.at("folder");
        }
        
        int port = 8080;
        if (ctx.flags.count("port")) {
            port = std::stoi(ctx.flags.at("port"));
        } else if (ctx.flags.count("p")) {
            port = std::stoi(ctx.flags.at("p"));
        }
        
        Logger::Log(LOG_INFO, "Starting server for folder: " + folder + " on port " + std::to_string(port));
        
        return start_server(folder, port);
    }
};

COMMAND(ServerCommand)
