#pragma once
#include "arg_base.h"

class HashingArgs : public ArgGroup {
public:
    const char* group() const override { 
        return "Hashing"; 
    }
    
    std::vector<ArgDef> definitions() const override {
        return {
            ArgDef("sha", "", "Enable SHA-256 hashing", true),
            ArgDef("sha256", "", "Enable SHA-256 hashing", true),
            ArgDef("crc", "", "Enable CRC32 hashing", true),
            ArgDef("crc32", "", "Enable CRC32 hashing", true),
        };
    }
};

ARG_GROUP(HashingArgs)
