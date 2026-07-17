#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include "../utils/logger/logger.h"

// All configuration that was previously spread across globals now constructed here.
struct AppConfig {
    std::string save_path;
    bool no_encrypt      = false;
    bool twofile_system  = false;
    bool grayscale       = false;
    bool isCompressed    = false;
    bool isPacked        = false;
    bool disableHash     = false;
    bool shaEnabled      = false;
    bool crcEnabled      = false;
    bool noRecursionFlag = false;
    bool exportInfoFlag  = false;
};

// Context passed to every command.
// config is a copy so commands can't accidentally mutate global state.
struct CommandContext {
    std::vector<std::string> args;
    std::map<std::string, std::string> flags;
    std::map<std::string, bool> boolFlags;
    std::string workingDir;
    AppConfig config;                    
};

// Base class for all commands
class Command {
public:
    virtual ~Command() = default;

    virtual const char* name()        const = 0;
    virtual const char* description() const = 0;
    virtual int run(CommandContext& ctx)    = 0;

    virtual const char* usage()  const { return ""; }
    virtual int minArgs()        const { return 0; }
    virtual int maxArgs()        const { return -1; }
};

// Registry singleton — unchanged
class CommandRegistry {
private:
    std::map<std::string, Command*> commands;
    CommandRegistry() = default;

public:
    static CommandRegistry& get() {
        static CommandRegistry instance;
        return instance;
    }

    void add(Command* cmd) {
        commands[cmd->name()] = cmd;
    }

    Command* find(const std::string& name) {
        auto it = commands.find(name);
        return it != commands.end() ? it->second : nullptr;
    }

    const std::map<std::string, Command*>& all() const {
        return commands;
    }
};

// Auto-registration helper
template<typename T>
class AutoRegister {
    static inline T instance;
public:
    AutoRegister() {
        CommandRegistry::get().add(&instance);
    }
};

#define COMMAND(ClassName) \
    static AutoRegister<ClassName> _auto_##ClassName;
    