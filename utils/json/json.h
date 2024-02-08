#ifndef JSON_READER_WRITER_H
#define JSON_READER_WRITER_H

#include <fstream>
#include <string>

struct Data {
    unsigned long long int binary_length;
    std::string original_filename;
};

void write_json(const std::string& path, const Data& data);
Data read_json(const std::string& path);

#endif // JSON_READER_WRITER_H
