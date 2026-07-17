#include <iostream>
#include <string>
#include <filesystem>
#include "cmds/command_base.h"
#include "cmds/command_loader.h"
#include "cmds/args/arg_loader.h"
#include "utils/logger/logger.h"
#include "version.h"

// Parse raw argv into a CommandContext (flags, boolFlags, positional args).
static CommandContext parseArgs(int argc, char* argv[]) {
    CommandContext ctx;

    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];

        if (arg.substr(0, 2) == "--") {
            std::string key = arg.substr(2);
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                ctx.flags[key] = argv[++i];
            } else {
                ctx.boolFlags[key] = true;
            }
        } else if (arg[0] == '-') {
            std::string key = arg.substr(1);
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                ctx.flags[key] = argv[++i];
            } else {
                ctx.boolFlags[key] = true;
            }
        } else {
            ctx.args.push_back(arg);
        }
    }

    return ctx;
}

// Build AppConfig from parsed flags.
// All logic that used to live in applyGlobalConfig() + the save_path

static AppConfig buildConfig(const CommandContext& ctx) {
    AppConfig cfg;

    cfg.no_encrypt      = ctx.boolFlags.count("no-encrypt") || ctx.boolFlags.count("ne");
    cfg.twofile_system  = ctx.boolFlags.count("two-file")   || ctx.boolFlags.count("2f");
    cfg.grayscale       = ctx.boolFlags.count("grayscale")  || ctx.boolFlags.count("gs");
    cfg.disableHash     = ctx.boolFlags.count("skip-hash")  || ctx.boolFlags.count("nh");
    cfg.shaEnabled      = ctx.boolFlags.count("sha")        || ctx.boolFlags.count("sha256");
    cfg.crcEnabled      = ctx.boolFlags.count("crc")        || ctx.boolFlags.count("crc32");
    cfg.noRecursionFlag = ctx.boolFlags.count("no-recursion")|| ctx.boolFlags.count("nr");
    cfg.exportInfoFlag  = ctx.boolFlags.count("export-info") || ctx.boolFlags.count("ei");

    if (ctx.flags.count("save-path"))
        cfg.save_path = ctx.flags.at("save-path");
    else if (ctx.flags.count("sp"))
        cfg.save_path = ctx.flags.at("sp");

    if (cfg.save_path.empty()) {
        Logger::Log(LOG_DEBUG, "Using default save path");
        const char* home;
#ifdef _WIN32
        home = std::getenv("USERPROFILE");
#else
        home = std::getenv("HOME");
#endif
        if (!home) {
            // Caller must handle the empty string as a fatal error.
            Logger::Log(LOG_ERROR, "Cannot determine home directory");
            return cfg;          // save_path stays empty — checked by caller
        }
        std::filesystem::path tool_dir = std::filesystem::path(home) / "CFETools";
        std::filesystem::create_directories(tool_dir);
        cfg.save_path = tool_dir.string();
    } else {
        Logger::Log(LOG_INFO, "Save path: " + cfg.save_path);
    }

    return cfg;
}

// ---------------------------------------------------------------------------
static void showHelp() {
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
    Logger::Log(LOG_INFO, "  --debug, -d          Enable debug logging");
    Logger::Log(LOG_INFO, "  --help, -h           Show this help message");
    Logger::Log(LOG_INFO, "  --version, -v        Show version information");
    Logger::Log(LOG_INFO, "  --save-path, -sp     Use a custom save path");
    Logger::Log(LOG_INFO, "");
    Logger::Log(LOG_INFO, "AVAILABLE ARG GROUPS:");

    for (auto& [groupName, argGroup] : ArgRegistry::get().all()) {
        Logger::Log(LOG_INFO, "");
        Logger::Log(LOG_INFO, "  " + std::string(argGroup->group()) + ":");
        for (auto& def : argGroup->definitions()) {
            std::string argLine = "    --" + def.longName;
            if (!def.shortName.empty()) argLine += ", -" + def.shortName;
            while (argLine.length() < 30) argLine += " ";
            argLine += def.description;
            Logger::Log(LOG_INFO, argLine);
        }
    }
}

// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    Logger::StartTimer("Total Execution");

    // Version flag — check before anything else
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-v" || arg == "--version" || arg == "version") {
            Logger::Log(LOG_INFO, "CFETools version: " VERSION);
            Logger::Log(LOG_INFO, "Author: unknownpersonog");
            return 0;
        }
    }

    // Log level
    Logger::SetLevel(LOG_INFO);
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-d" || arg == "--debug") {
            Logger::SetLevel(LOG_DEBUG);
            break;
        }
    }

    if (argc < 2) {
        showHelp();
        return 0;
    }

    std::string cmdName = argv[1];

    if (cmdName == "-h" || cmdName == "--help" || cmdName == "help") {
        showHelp();
        return 0;
    }

    Command* cmd = CommandRegistry::get().find(cmdName);
    if (!cmd) {
        Logger::Log(LOG_ERROR, "Unknown command: " + cmdName);
        Logger::Log(LOG_INFO, "Use --help to see available commands");
        return 1;
    }

    // Parse then build config — both are pure functions of argv
    CommandContext ctx = parseArgs(argc, argv);
    ctx.config = buildConfig(ctx);

    if (ctx.config.save_path.empty()) {
        // buildConfig already logged the error
        return 1;
    }

    ctx.workingDir = ctx.config.save_path;

    // Per-command help
    if (ctx.boolFlags.count("help") || ctx.boolFlags.count("h")) {
        Logger::Log(LOG_INFO, "Command: "     + std::string(cmd->name()));
        Logger::Log(LOG_INFO, "Description: " + std::string(cmd->description()));
        Logger::Log(LOG_INFO, "Usage: "       + std::string(cmd->usage()));
        return 0;
    }

    if (ctx.args.size() < static_cast<size_t>(cmd->minArgs())) {
        Logger::Log(LOG_ERROR, "Not enough arguments");
        Logger::Log(LOG_INFO, "Usage: " + std::string(cmd->usage()));
        return 1;
    }

    int result = cmd->run(ctx);
    Logger::EndTimer("Total Execution", LOG_INFO);
    return result;
}