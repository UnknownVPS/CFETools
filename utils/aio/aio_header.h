#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <cmath>

enum class AIOHeaderType : uint8_t {
    UINT8 = 0x01,
    UINT16 = 0x02,
    UINT32 = 0x04,
    UINT64 = 0x08,
    STRING = 0x10,
    BOOL = 0x20,
    FLOAT = 0x30,
    DOUBLE = 0x40
};

struct AIOHeaderEntry {
    AIOHeaderType type;
    uint32_t bitSize;        // Dynamically calculated minimum bits needed
    uint32_t bitOffset;      // Offset in BITS from start of data section
    std::string name;        // Field name for identification
};

class AIOHeaderWriter {
public:
    AIOHeaderWriter();
    
    // Auto-calculate minimum bits needed for each value
    void addUInt8(const std::string& name, uint8_t value);
    void addUInt16(const std::string& name, uint16_t value);
    void addUInt32(const std::string& name, uint32_t value);
    void addUInt64(const std::string& name, uint64_t value);
    void addString(const std::string& name, const std::string& value);
    void addBool(const std::string& name, bool value);
    void addFloat(const std::string& name, float value);
    void addDouble(const std::string& name, double value);
    
    // Manual override for specific bit sizes (optional)
    void addUIntCustom(const std::string& name, uint64_t value, uint32_t forcedBitSize);
    
    // Write the complete header to a stream
    void writeToStream(std::ofstream& stream);
    
    // Get total header size in bytes
    size_t getTotalSize() const;
    
    // Clear all entries
    void clear();

private:
    std::vector<AIOHeaderEntry> entries_;
    std::vector<uint8_t> dataBuffer_;
    uint32_t currentBitOffset_;
    
    // Calculate minimum bits needed for a value
    uint32_t calculateBitsNeeded(uint64_t value) const;
    
    void writeBitsToBuffer(uint64_t value, uint32_t bitSize);
    void writeStringToBuffer(const std::string& str);
};

class AIOHeaderReader {
public:
    AIOHeaderReader();
    
    // Read header from stream
    bool readFromStream(std::ifstream& stream);
    
    // Get values by name
    bool getUInt8(const std::string& name, uint8_t& value) const;
    bool getUInt16(const std::string& name, uint16_t& value) const;
    bool getUInt32(const std::string& name, uint32_t& value) const;
    bool getUInt64(const std::string& name, uint64_t& value) const;
    bool getString(const std::string& name, std::string& value) const;
    bool getBool(const std::string& name, bool& value) const;
    bool getFloat(const std::string& name, float& value) const;
    bool getDouble(const std::string& name, double& value) const;
    
    // Get any integer value as uint64_t (handles all int types dynamically)
    bool getUInt(const std::string& name, uint64_t& value) const;
    
    // Check if field exists
    bool hasField(const std::string& name) const;
    
    // Get all field names
    std::vector<std::string> getFieldNames() const;
    
    // Get field info (for debugging)
    bool getFieldInfo(const std::string& name, AIOHeaderEntry& entry) const;

private:
    std::unordered_map<std::string, AIOHeaderEntry> entries_;
    std::vector<uint8_t> dataBuffer_;
    
    uint64_t readBitsFromBuffer(uint32_t bitOffset, uint32_t bitSize) const;
    std::string readStringFromBuffer(uint32_t bitOffset, uint32_t bitSize) const;
};
