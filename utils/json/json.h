#ifndef JSON_READER_WRITER_H
#define JSON_READER_WRITER_H

#include <fstream>
#include <string>
#include <cstdint>

struct Data {
    uint_fast64_t binary_length;
    std::string original_filename;
    std::string encryption_key;
};

void write_json(const std::string& path, const Data& data);
Data read_json(const std::string& path);

#endif // JSON_READER_WRITER_H
