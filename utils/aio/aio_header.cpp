#include "aio_header.h"
#include <algorithm>
#include <stdexcept>
#include <bit>
 
constexpr uint32_t AIO_MAGIC = 0x41494F48; // "AIOH" in little endian
constexpr uint16_t AIO_VERSION = 0x0001;

AIOHeaderWriter::AIOHeaderWriter() : currentBitOffset_(0) {
    dataBuffer_.reserve(4096);
}

uint32_t AIOHeaderWriter::calculateBitsNeeded(uint64_t value) const {
    if (value == 0) return 1; // Need at least 1 bit to represent 0
    
    // Use bit width calculation: floor(log2(value)) + 1
    return static_cast<uint32_t>(64 - std::countl_zero(value));
}

void AIOHeaderWriter::addUInt8(const std::string& name, uint8_t value) {
    uint32_t bitsNeeded = calculateBitsNeeded(static_cast<uint64_t>(value));
    
    AIOHeaderEntry entry;
    entry.type = AIOHeaderType::UINT8;
    entry.bitSize = bitsNeeded;
    entry.bitOffset = currentBitOffset_;
    entry.name = name;
    
    entries_.push_back(entry);
    writeBitsToBuffer(static_cast<uint64_t>(value), bitsNeeded);
}

void AIOHeaderWriter::addUInt16(const std::string& name, uint16_t value) {
    uint32_t bitsNeeded = calculateBitsNeeded(static_cast<uint64_t>(value));
    
    AIOHeaderEntry entry;
    entry.type = AIOHeaderType::UINT16;
    entry.bitSize = bitsNeeded;
    entry.bitOffset = currentBitOffset_;
    entry.name = name;
    
    entries_.push_back(entry);
    writeBitsToBuffer(static_cast<uint64_t>(value), bitsNeeded);
}

void AIOHeaderWriter::addUInt32(const std::string& name, uint32_t value) {
    uint32_t bitsNeeded = calculateBitsNeeded(static_cast<uint64_t>(value));
    
    AIOHeaderEntry entry;
    entry.type = AIOHeaderType::UINT32;
    entry.bitSize = bitsNeeded;
    entry.bitOffset = currentBitOffset_;
    entry.name = name;
    
    entries_.push_back(entry);
    writeBitsToBuffer(static_cast<uint64_t>(value), bitsNeeded);
}

void AIOHeaderWriter::addUInt64(const std::string& name, uint64_t value) {
    uint32_t bitsNeeded = calculateBitsNeeded(value);
    
    AIOHeaderEntry entry;
    entry.type = AIOHeaderType::UINT64;
    entry.bitSize = bitsNeeded;
    entry.bitOffset = currentBitOffset_;
    entry.name = name;
    
    entries_.push_back(entry);
    writeBitsToBuffer(value, bitsNeeded);
}

void AIOHeaderWriter::addString(const std::string& name, const std::string& value) {
    AIOHeaderEntry entry;
    entry.type = AIOHeaderType::STRING;
    entry.bitSize = static_cast<uint32_t>(value.length() * 8);
    entry.bitOffset = currentBitOffset_;
    entry.name = name;
    
    entries_.push_back(entry);
    writeStringToBuffer(value);
}

void AIOHeaderWriter::addBool(const std::string& name, bool value) {
    AIOHeaderEntry entry;
    entry.type = AIOHeaderType::BOOL;
    entry.bitSize = 1; // Always 1 bit for boolean
    entry.bitOffset = currentBitOffset_;
    entry.name = name;
    
    entries_.push_back(entry);
    writeBitsToBuffer(value ? 1 : 0, 1);
}

void AIOHeaderWriter::addFloat(const std::string& name, float value) {
    AIOHeaderEntry entry;
    entry.type = AIOHeaderType::FLOAT;
    entry.bitSize = 32; // Always 32 bits for IEEE 754 float
    entry.bitOffset = currentBitOffset_;
    entry.name = name;
    
    entries_.push_back(entry);
    
    uint32_t floatBits;
    memcpy(&floatBits, &value, sizeof(float));
    writeBitsToBuffer(static_cast<uint64_t>(floatBits), 32);
}

void AIOHeaderWriter::addDouble(const std::string& name, double value) {
    AIOHeaderEntry entry;
    entry.type = AIOHeaderType::DOUBLE;
    entry.bitSize = 64; // Always 64 bits for IEEE 754 double
    entry.bitOffset = currentBitOffset_;
    entry.name = name;
    
    entries_.push_back(entry);
    
    uint64_t doubleBits;
    memcpy(&doubleBits, &value, sizeof(double));
    writeBitsToBuffer(doubleBits, 64);
}

void AIOHeaderWriter::addUIntCustom(const std::string& name, uint64_t value, uint32_t forcedBitSize) {
    // Determine the appropriate type based on the value range
    AIOHeaderType type;
    if (value <= UINT8_MAX) type = AIOHeaderType::UINT8;
    else if (value <= UINT16_MAX) type = AIOHeaderType::UINT16;
    else if (value <= UINT32_MAX) type = AIOHeaderType::UINT32;
    else type = AIOHeaderType::UINT64;
    
    // Ensure forced bit size can actually hold the value
    uint32_t minBitsNeeded = calculateBitsNeeded(value);
    if (forcedBitSize < minBitsNeeded) {
        forcedBitSize = minBitsNeeded; // Auto-correct to minimum needed
    }
    
    AIOHeaderEntry entry;
    entry.type = type;
    entry.bitSize = forcedBitSize;
    entry.bitOffset = currentBitOffset_;
    entry.name = name;
    
    entries_.push_back(entry);
    writeBitsToBuffer(value, forcedBitSize);
}

void AIOHeaderWriter::writeBitsToBuffer(uint64_t value, uint32_t bitSize) {
    uint32_t byteOffset = currentBitOffset_ / 8;
    uint32_t bitInByte = currentBitOffset_ % 8;
    
    // Ensure buffer is large enough
    uint32_t requiredBytes = (currentBitOffset_ + bitSize + 7) / 8;
    if (dataBuffer_.size() < requiredBytes) {
        dataBuffer_.resize(requiredBytes, 0);
    }
    
    // Write bits (little-endian bit order within bytes)
    for (uint32_t i = 0; i < bitSize; i++) {
        uint32_t currentByteOffset = byteOffset + (bitInByte + i) / 8;
        uint32_t currentBitInByte = (bitInByte + i) % 8;
        
        if (value & (1ULL << i)) {
            dataBuffer_[currentByteOffset] |= (1 << currentBitInByte);
        }
    }
    
    currentBitOffset_ += bitSize;
}

void AIOHeaderWriter::writeStringToBuffer(const std::string& str) {
    for (char c : str) {
        writeBitsToBuffer(static_cast<uint64_t>(static_cast<uint8_t>(c)), 8);
    }
}

void AIOHeaderWriter::writeToStream(std::ofstream& stream) {
    // Write magic number and version
    stream.write(reinterpret_cast<const char*>(&AIO_MAGIC), sizeof(AIO_MAGIC));
    stream.write(reinterpret_cast<const char*>(&AIO_VERSION), sizeof(AIO_VERSION));
    
    // Write entry count
    uint32_t entryCount = static_cast<uint32_t>(entries_.size());
    stream.write(reinterpret_cast<const char*>(&entryCount), sizeof(entryCount));
    
    // Write data buffer size in bytes
    uint32_t dataSize = static_cast<uint32_t>(dataBuffer_.size());
    stream.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
    
    // Write entries directory
    for (const auto& entry : entries_) {
        stream.write(reinterpret_cast<const char*>(&entry.type), sizeof(entry.type));
        stream.write(reinterpret_cast<const char*>(&entry.bitSize), sizeof(entry.bitSize));
        stream.write(reinterpret_cast<const char*>(&entry.bitOffset), sizeof(entry.bitOffset));
        
        uint16_t nameLen = static_cast<uint16_t>(entry.name.length());
        stream.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
        stream.write(entry.name.data(), nameLen);
    }
    
    // Write data buffer
    stream.write(reinterpret_cast<const char*>(dataBuffer_.data()), dataSize);
}

size_t AIOHeaderWriter::getTotalSize() const {
    size_t size = sizeof(AIO_MAGIC) + sizeof(AIO_VERSION) + sizeof(uint32_t) + sizeof(uint32_t);
    
    for (const auto& entry : entries_) {
        size += sizeof(entry.type) + sizeof(entry.bitSize) + sizeof(entry.bitOffset);
        size += sizeof(uint16_t) + entry.name.length();
    }
    
    size += dataBuffer_.size();
    return size;
}

void AIOHeaderWriter::clear() {
    entries_.clear();
    dataBuffer_.clear();
    currentBitOffset_ = 0;
}

// AIOHeaderReader implementation
AIOHeaderReader::AIOHeaderReader() {}

bool AIOHeaderReader::readFromStream(std::ifstream& stream) {
    uint32_t magic;
    stream.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (magic != AIO_MAGIC) return false;
    
    uint16_t version;
    stream.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (version != AIO_VERSION) return false;
    
    uint32_t entryCount;
    stream.read(reinterpret_cast<char*>(&entryCount), sizeof(entryCount));
    
    uint32_t dataSize;
    stream.read(reinterpret_cast<char*>(&dataSize), sizeof(dataSize));
    
    entries_.clear();
    for (uint32_t i = 0; i < entryCount; i++) {
        AIOHeaderEntry entry;
        stream.read(reinterpret_cast<char*>(&entry.type), sizeof(entry.type));
        stream.read(reinterpret_cast<char*>(&entry.bitSize), sizeof(entry.bitSize));
        stream.read(reinterpret_cast<char*>(&entry.bitOffset), sizeof(entry.bitOffset));
        
        uint16_t nameLen;
        stream.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));
        entry.name.resize(nameLen);
        stream.read(&entry.name[0], nameLen);
        
        entries_[entry.name] = entry;
    }
    
    dataBuffer_.resize(dataSize);
    stream.read(reinterpret_cast<char*>(dataBuffer_.data()), dataSize);
    
    return true;
}

uint64_t AIOHeaderReader::readBitsFromBuffer(uint32_t bitOffset, uint32_t bitSize) const {
    uint64_t result = 0;
    uint32_t byteOffset = bitOffset / 8;
    uint32_t bitInByte = bitOffset % 8;
    
    for (uint32_t i = 0; i < bitSize && i < 64; i++) {
        uint32_t currentByteOffset = byteOffset + (bitInByte + i) / 8;
        uint32_t currentBitInByte = (bitInByte + i) % 8;
        
        if (currentByteOffset < dataBuffer_.size()) {
            if (dataBuffer_[currentByteOffset] & (1 << currentBitInByte)) {
                result |= (1ULL << i);
            }
        }
    }
    
    return result;
}

std::string AIOHeaderReader::readStringFromBuffer(uint32_t bitOffset, uint32_t bitSize) const {
    std::string result;
    uint32_t byteCount = bitSize / 8;
    result.reserve(byteCount);
    
    for (uint32_t i = 0; i < byteCount; i++) {
        uint64_t charValue = readBitsFromBuffer(bitOffset + i * 8, 8);
        result.push_back(static_cast<char>(charValue));
    }
    
    return result;
}

// Generic getter that works for all integer types
bool AIOHeaderReader::getUInt(const std::string& name, uint64_t& value) const {
    auto it = entries_.find(name);
    if (it == entries_.end()) return false;
    
    // Handle all integer types uniformly
    switch (it->second.type) {
        case AIOHeaderType::UINT8:
        case AIOHeaderType::UINT16:
        case AIOHeaderType::UINT32:
        case AIOHeaderType::UINT64:
        case AIOHeaderType::BOOL:
            value = readBitsFromBuffer(it->second.bitOffset, it->second.bitSize);
            return true;
        default:
            return false;
    }
}

bool AIOHeaderReader::getUInt8(const std::string& name, uint8_t& value) const {
    uint64_t temp;
    if (getUInt(name, temp)) {
        value = static_cast<uint8_t>(temp);
        return true;
    }
    return false;
}

bool AIOHeaderReader::getUInt16(const std::string& name, uint16_t& value) const {
    uint64_t temp;
    if (getUInt(name, temp)) {
        value = static_cast<uint16_t>(temp);
        return true;
    }
    return false;
}

bool AIOHeaderReader::getUInt32(const std::string& name, uint32_t& value) const {
    uint64_t temp;
    if (getUInt(name, temp)) {
        value = static_cast<uint32_t>(temp);
        return true;
    }
    return false;
}

bool AIOHeaderReader::getUInt64(const std::string& name, uint64_t& value) const {
    return getUInt(name, value);
}

bool AIOHeaderReader::getString(const std::string& name, std::string& value) const {
    auto it = entries_.find(name);
    if (it == entries_.end() || it->second.type != AIOHeaderType::STRING) return false;
    
    value = readStringFromBuffer(it->second.bitOffset, it->second.bitSize);
    return true;
}

bool AIOHeaderReader::getBool(const std::string& name, bool& value) const {
    uint64_t temp;
    if (getUInt(name, temp)) {
        value = temp != 0;
        return true;
    }
    return false;
}

bool AIOHeaderReader::getFloat(const std::string& name, float& value) const {
    auto it = entries_.find(name);
    if (it == entries_.end() || it->second.type != AIOHeaderType::FLOAT) return false;
    
    uint32_t floatBits = static_cast<uint32_t>(readBitsFromBuffer(it->second.bitOffset, it->second.bitSize));
    memcpy(&value, &floatBits, sizeof(float));
    return true;
}

bool AIOHeaderReader::getDouble(const std::string& name, double& value) const {
    auto it = entries_.find(name);
    if (it == entries_.end() || it->second.type != AIOHeaderType::DOUBLE) return false;
    
    uint64_t doubleBits = readBitsFromBuffer(it->second.bitOffset, it->second.bitSize);
    memcpy(&value, &doubleBits, sizeof(double));
    return true;
}

bool AIOHeaderReader::hasField(const std::string& name) const {
    return entries_.find(name) != entries_.end();
}

std::vector<std::string> AIOHeaderReader::getFieldNames() const {
    std::vector<std::string> names;
    names.reserve(entries_.size());
    for (const auto& pair : entries_) {
        names.push_back(pair.first);
    }
    return names;
}

bool AIOHeaderReader::getFieldInfo(const std::string& name, AIOHeaderEntry& entry) const {
    auto it = entries_.find(name);
    if (it == entries_.end()) return false;
    entry = it->second;
    return true;
}
