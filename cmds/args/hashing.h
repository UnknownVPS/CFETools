#pragma once
#include "arg_base.h"

class HashingArgs : public ArgGroup {
public:
    const char* group() const override { 
        return "Hashing"; 
    }
    
    std::vector<ArgDef> definitions() const override {
        return {
            ArgDef("sha256", "sha", "Enable SHA-256 hashing", true),
            ArgDef("crc32", "crc", "Enable CRC32 hashing", true),
            ArgDef("no-recursion", "nr", "Process folders without recursion", true),
            ArgDef("export-info", "ei", "Export folder hash info to a text file", true),
        };
    }
};

ARG_GROUP(HashingArgs)
