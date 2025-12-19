#pragma once
#include "command_base.h"
#include "../func/file_server/server.h"
#include <filesystem>

extern std::string save_path;

class ServerCommand : public Command {
public:
    const char* name() const override { 
        return "server"; 
    }
    
    const char* description() const override { 
        return "Host a file server"; 
    }
    
    const char* usage() const override { 
        return "server <port> <path>"; 
    }
    
    int minArgs() const override { 
        return 2; 
    }
    
    int run(CommandContext& ctx) override {
        int port = std::stoi(ctx.args[0]);
        
        std::string path = ctx.args[1];
                
        try {
            FileServer server(port, path);
            server.start();
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return 1;
        }

        return 0;
    }
};

COMMAND(ServerCommand)