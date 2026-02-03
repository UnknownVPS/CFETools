#pragma once
#include "arg_base.h"

class ServerArgs : public ArgGroup {
public:
    const char* group() const override { 
        return "Server"; 
    }
    
    std::vector<ArgDef> definitions() const override {
        return {
            ArgDef("threads", "t", "Number of server threads", false),
            ArgDef("port", "p", "Port number to run the server on", false, "8080"),
            ArgDef("pagesize", "ps", "Page size for pagination", false),
            ArgDef("symlinks", "sl", "Enable following symlinks", true),
        };
    }
};

ARG_GROUP(ServerArgs)
