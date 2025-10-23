#pragma once
#include "arg_base.h"

class __CLASSNAME__Args : public ArgGroup {
public:
    const char* group() const override { 
        return "__GROUP__"; 
    }
    
    std::vector<ArgDef> definitions() const override {
        return {
            // ArgDef("long-name", "short", "Description", isFlag, "default")
            // Example: ArgDef("compress", "c", "Compression level", false, "0")
            // Example: ArgDef("verbose", "v", "Enable verbose output", true)
        };
    }
};

ARG_GROUP(__CLASSNAME__Args)
