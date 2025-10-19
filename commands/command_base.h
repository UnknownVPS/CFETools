#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include "../utils/logger/logger.h"

// Context passed to every command
struct CommandContext {
    std::vector<std::string> args;              // Positional arguments
    std::map<std::string, std::string> flags;   // --flag value
    std::map<std::string, bool> boolFlags;      // --flag (no value)
    std::string workingDir;                     // Working directory
};

// Base class for all commands
class Command {
public:
    virtual ~Command() = default;
    
    // Required overrides
    virtual const char* name() const = 0;
    virtual const char* description() const = 0;
    virtual int run(CommandContext& ctx) = 0;
    
    // Optional overrides
    virtual const char* usage() const { return ""; }
    virtual int minArgs() const { return 0; }
    virtual int maxArgs() const { return -1; } // -1 = unlimited
};

// Registry singleton
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
