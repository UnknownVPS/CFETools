#pragma once
#include <string>
#include <vector>
#include <map>
#include <functional>

// Argument definition
struct ArgDef {
    std::string longName;
    std::string shortName;
    std::string description;
    std::string defaultValue;
    bool isFlag;  // true = boolean flag, false = value flag
    
    ArgDef(const std::string& lname, const std::string& sname, 
           const std::string& desc, bool flag = false, 
           const std::string& defval = "")
        : longName(lname), shortName(sname), description(desc),
          defaultValue(defval), isFlag(flag) {}
};

// Base class for argument groups
class ArgGroup {
public:
    virtual ~ArgGroup() = default;
    virtual const char* group() const = 0;
    virtual std::vector<ArgDef> definitions() const = 0;
};

// Argument registry
class ArgRegistry {
private:
    std::map<std::string, ArgGroup*> groups;
    ArgRegistry() = default;
    
public:
    static ArgRegistry& get() {
        static ArgRegistry instance;
        return instance;
    }
    
    void add(ArgGroup* grp) {
        groups[grp->group()] = grp;
    }
    
    const std::map<std::string, ArgGroup*>& all() const {
        return groups;
    }
};

// Auto-registration helper
template<typename T>
class AutoRegisterArg {
    static inline T instance;
public:
    AutoRegisterArg() {
        ArgRegistry::get().add(&instance);
    }
};

#define ARG_GROUP(ClassName) \
    static AutoRegisterArg<ClassName> _auto_arg_##ClassName;
