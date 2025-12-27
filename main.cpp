#include <iostream>
#include <string>
#include <filesystem>
#include "cmds/command_base.h"
#include "cmds/command_loader.h"  // Auto-includes all commands
#include "cmds/args/arg_loader.h" // Auto-includes all arg groups
#include "utils/logger/logger.h"
#include "globals.h"
#include "version.h"

// Global configuration flags
bool no_encrypt = false;
bool twofile_system = false; 
bool grayscale = false;
bool isCompressed = false;
bool isPacked = false;
bool disableHash = false;
std::string save_path = "";
bool shaEnabled = false;
bool crcEnabled = false;
bool noRecursionFlag = false;
bool exportInfoFlag = false;

// Parse command line arguments into context
CommandContext parseArgs(int argc, char* argv[]) {
    CommandContext ctx;
    
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg.substr(0, 2) == "--") {
            std::string key = arg.substr(2);
            
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                ctx.flags[key] = argv[i + 1];
                i++;
            } else {
                ctx.boolFlags[key] = true;
            }
        } else if (arg[0] == '-') {
            std::string key = arg.substr(1);
            
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                ctx.flags[key] = argv[i + 1];
                i++;
            } else {
                ctx.boolFlags[key] = true;
            }
        } else {
            ctx.args.push_back(arg);
        }
    }
    
    ctx.workingDir = save_path;
    return ctx;
}

// Apply global configuration from flags
void applyGlobalConfig(const CommandContext& ctx) {
    no_encrypt = ctx.boolFlags.count("no-encrypt") || ctx.boolFlags.count("ne");
    twofile_system = ctx.boolFlags.count("two-file") || ctx.boolFlags.count("2f");
    grayscale = ctx.boolFlags.count("grayscale") || ctx.boolFlags.count("gs");
    disableHash = ctx.boolFlags.count("skip-hash") || ctx.boolFlags.count("nh");
    shaEnabled = ctx.boolFlags.count("sha") || ctx.boolFlags.count("sha256");
    crcEnabled = ctx.boolFlags.count("crc") || ctx.boolFlags.count("crc32");
    save_path = ctx.flags.count("save-path") ? ctx.flags.at("save-path") :
                (ctx.flags.count("sp") ? ctx.flags.at("sp") : save_path);
    noRecursionFlag = ctx.boolFlags.count("no-recursion") || ctx.boolFlags.count("nr");
    exportInfoFlag = ctx.boolFlags.count("export-info") || ctx.boolFlags.count("ei");
}

// Show help for all commands
void showHelp() {
    Logger::Log(LOG_INFO, "CFETools - Comprehensive File Encoding Tools");
    Logger::Log(LOG_INFO, "");
    Logger::Log(LOG_INFO, "COMMANDS:");
    
    for (auto& [name, cmd] : CommandRegistry::get().all()) {
        std::string line = "  " + name;
        while (line.length() < 15) line += " ";
        line += cmd->description();
        Logger::Log(LOG_INFO, line);
    }
    
    Logger::Log(LOG_INFO, "");
    Logger::Log(LOG_INFO, "USAGE:");
    Logger::Log(LOG_INFO, "  cfx <command> [arguments] [options]");
    Logger::Log(LOG_INFO, "  cfx <command> --help");
    Logger::Log(LOG_INFO, "");
    Logger::Log(LOG_INFO, "GLOBAL OPTIONS:");
    Logger::Log(LOG_INFO, "  -d, --debug          Enable debug logging");
    Logger::Log(LOG_INFO, "  -h, --help           Show this help message");
    Logger::Log(LOG_INFO, "  -v, --version        Show version information");
    Logger::Log(LOG_INFO, "  -sp, --save-path     Use a custom save path");
    Logger::Log(LOG_INFO, "");
    Logger::Log(LOG_INFO, "AVAILABLE ARG GROUPS:");
    
    for (auto& [groupName, argGroup] : ArgRegistry::get().all()) {
        Logger::Log(LOG_INFO, "");
        Logger::Log(LOG_INFO, "  " + std::string(argGroup->group()) + ":");
        for (auto& def : argGroup->definitions()) {
            std::string argLine = "    --" + def.longName;
            if (!def.shortName.empty()) {
                argLine += ", -" + def.shortName;
            }
            while (argLine.length() < 30) argLine += " ";
            argLine += def.description;
            Logger::Log(LOG_INFO, argLine);
        }
    }
}

int main(int argc, char* argv[]) {
    Logger::StartTimer("Total Execution");
    
    // Handle version flag
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-v" || arg == "--version" || arg == "version") {
            Logger::Log(LOG_INFO, "CFETools version: " VERSION);
            Logger::Log(LOG_INFO, "Author: unknownpersonog");
            return 0;
        }
    }
    
    // Set debug level
    Logger::SetLevel(LOG_INFO);
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-d" || arg == "--debug") {
            Logger::SetLevel(LOG_DEBUG);
            break;
        }
    }

    // Show help if no command
    if (argc < 2) {
        showHelp();
        return 0;
    }
    
    std::string cmdName = argv[1];
    
    // Handle help flag
    if (cmdName == "-h" || cmdName == "--help" || cmdName == "help") {
        showHelp();
        return 0;
    }
    
    // Find command
    Command* cmd = CommandRegistry::get().find(cmdName);
    
    if (!cmd) {
        Logger::Log(LOG_ERROR, "Unknown command: " + cmdName);
        Logger::Log(LOG_INFO, "Use --help to see available commands");
        return 1;
    }
    
    // Parse arguments
    CommandContext ctx = parseArgs(argc, argv);
    
    // Apply global configuration
    applyGlobalConfig(ctx);
    if (save_path.empty()) {
        Logger::Log(LOG_DEBUG, "Using default save path");
        
        // Setup working directory
        const char* home;
        #ifdef _WIN32
            home = std::getenv("USERPROFILE");
        #else
            home = std::getenv("HOME");
        #endif
        
        if (home != nullptr) {
            std::filesystem::path tool_dir(home);
            tool_dir /= "CFETools";
            if (!std::filesystem::exists(tool_dir)) {
                std::filesystem::create_directory(tool_dir);
            }
            save_path = tool_dir.string();
        } else {
            Logger::Log(LOG_ERROR, "Cannot determine home directory");
            return 1;
        }
    } else {
        Logger::Log(LOG_INFO, "Save path: " + save_path);
    }    
    // Show command-specific help if requested
    if (ctx.boolFlags["help"] || ctx.boolFlags["h"]) {
        Logger::Log(LOG_INFO, "Command: " + std::string(cmd->name()));
        Logger::Log(LOG_INFO, "Description: " + std::string(cmd->description()));
        Logger::Log(LOG_INFO, "Usage: " + std::string(cmd->usage()));
        return 0;
    }
    
    // Validate argument count
    if (ctx.args.size() < static_cast<size_t>(cmd->minArgs())) {
        Logger::Log(LOG_ERROR, "Not enough arguments");
        Logger::Log(LOG_INFO, "Usage: " + std::string(cmd->usage()));
        return 1;
    }
    
    // Execute command
    int result = cmd->run(ctx);
    
    Logger::EndTimer("Total Execution", LOG_INFO);
    return result;
}
