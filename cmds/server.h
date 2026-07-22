#pragma once
#include "command_base.h"
#include "../func/server/server_core.h"
#include "../func/server/dir_module.h"
#include "../func/server/cfup_module.h"
#include "../func/folder_packer/folder_packer.h"
#include <iostream>
#include <thread>
#include <filesystem>

namespace fs = std::filesystem;

class ServerCommand : public Command {
public:
    const char* name() const override { 
        return "server"; 
    }
    
    const char* description() const override { 
        return "Start a static file server or CFUP archive server"; 
    }
    
    const char* usage() const override { 
        return "server [folder_or_archive] [--port <port>]"; 
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
        
        bool symlinks_enabled = false;
        if (ctx.boolFlags.count("symlinks")) {
            symlinks_enabled = ctx.boolFlags.at("symlinks");
        } else if (ctx.boolFlags.count("sl")) {
            symlinks_enabled = ctx.boolFlags.at("sl");
        }

        // Resolve to absolute path
        std::error_code ec;
        fs::path abs_path = fs::absolute(folder, ec);
        if (ec) {
            Logger::Log(LOG_ERROR, "Failed to resolve path: " + folder);
            return 1;
        }
        abs_path = abs_path.lexically_normal();

        if (!fs::exists(abs_path, ec)) {
            Logger::Log(LOG_ERROR, "Path does not exist: " + abs_path.string());
            return 1;
        }

        ServerConfig cfg;
        cfg.root_path = abs_path.string();
        cfg.port = port;
        cfg.page_size = page_size;
        cfg.thread_pool_size = threads;
        cfg.follow_symlinks = symlinks_enabled;

        ServerCore core(cfg);

        bool is_cfup_file = fs::is_regular_file(abs_path, ec);
        if (is_cfup_file) {
            if (!looks_like_cfup(abs_path.string())) {
                Logger::Log(LOG_ERROR, "File is not a valid CFUP archive: " + abs_path.string());
                return 1;
            }
            
            // Pass the parent directory as fs_root so /__cfup_open__ can resolve relative files if needed
            std::string fs_root = abs_path.parent_path().string();
            if (fs_root.empty()) fs_root = ".";
            
            // Mount the CFUP module as the primary handler
            core.register_module(std::make_shared<CfupModule>(abs_path.string(), fs_root, page_size));
            Logger::Log(LOG_INFO, "Starting CFUP archive server for: " + abs_path.string() + 
                                 " on port " + std::to_string(port) + 
                                 " with page size " + std::to_string(page_size));
        } else {
            // Mount the Dir module as the primary handler
            core.register_module(std::make_shared<DirModule>(abs_path.string(), symlinks_enabled, page_size));
            // Also mount the CFUP module with an empty path but the root path so it can intercept `/__cfup_open__` routes
            core.register_module(std::make_shared<CfupModule>("", abs_path.string(), page_size));
            
            Logger::Log(LOG_INFO, "Starting directory server for: " + abs_path.string() + 
                                 " on port " + std::to_string(port) + 
                                 " with page size " + std::to_string(page_size) + 
                                 " and thread pool size " + std::to_string(threads) + 
                                 " and symlinks enabled: " + (symlinks_enabled ? "true" : "false"));
        }
        
        return core.run();
    }
};

COMMAND(ServerCommand)