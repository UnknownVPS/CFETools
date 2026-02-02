#pragma once
#include "command_base.h"
#include "../func/server/server.h"
#include <iostream>
#include <thread>
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
        size_t page_size = 100;
        if (ctx.flags.count("pagesize")) {
            page_size = std::stoul(ctx.flags.at("pagesize"));
        } else if (ctx.flags.count("ps")) {
            page_size = std::stoul(ctx.flags.at("ps"));
        }

        int threads = 1;
        if (ctx.flags.count("threads")) {
            threads = std::stoi(ctx.flags.at("threads"));
        } else if (ctx.flags.count("t")) {
            threads = std::stoi(ctx.flags.at("t"));
        } else if (std::thread::hardware_concurrency() > 0) {
            threads = static_cast<int>(std::thread::hardware_concurrency());
        }
        Logger::Log(LOG_INFO, "Starting server for folder: " + folder + " on port " + std::to_string(port) + " with page size " + std::to_string(page_size) + " and thread pool size " + std::to_string(threads));
        
        return start_server(folder, port, page_size, threads);
    }
};

COMMAND(ServerCommand)
