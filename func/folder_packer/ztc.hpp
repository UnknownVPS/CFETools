#ifndef ZIP_TO_CFUP_CONVERTER_H
#define ZIP_TO_CFUP_CONVERTER_H

#include <string>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <zlib.h>
#include "../../utils/logger/logger.h"

namespace fs = std::filesystem;

// ZIP file format structures (little-endian)
#pragma pack(push, 1)
struct ZipLocalFileHeader {
    uint32_t signature;           // 0x04034b50
    uint16_t version;             // Version needed to extract
    uint16_t flags;               // General purpose bit flag
    uint16_t compression;         // Compression method (0=store, 8=deflate)
    uint16_t mod_time;            // Last mod file time
    uint16_t mod_date;            // Last mod file date
    uint32_t crc32;               // CRC-32
    uint32_t compressed_size;     // Compressed size
    uint32_t uncompressed_size;   // Uncompressed size
    uint16_t filename_len;        // File name length
    uint16_t extra_len;           // Extra field length
};

struct ZipCentralDirHeader {
    uint32_t signature;           // 0x02014b50
    uint16_t version_made;        // Version made by
    uint16_t version_needed;      // Version needed to extract
    uint16_t flags;               // General purpose bit flag
    uint16_t compression;         // Compression method
    uint16_t mod_time;            // Last mod file time
    uint16_t mod_date;            // Last mod file date
    uint32_t crc32;               // CRC-32
    uint32_t compressed_size;     // Compressed size
    uint32_t uncompressed_size;   // Uncompressed size
    uint16_t filename_len;        // File name length
    uint16_t extra_len;           // Extra field length
    uint16_t comment_len;         // File comment length
    uint16_t disk_start;          // Disk number start
    uint16_t internal_attr;       // Internal file attributes
    uint32_t external_attr;       // External file attributes
    uint32_t local_header_offset; // Relative offset of local header
};

struct ZipEndOfCentralDir {
    uint32_t signature;           // 0x06054b50
    uint16_t disk_num;            // Number of this disk
    uint16_t cd_start_disk;       // Disk where central directory starts
    uint16_t cd_records_disk;     // Number of central directory records on this disk
    uint16_t cd_records_total;    // Total number of central directory records
    uint32_t cd_size;             // Size of central directory
    uint32_t cd_offset;           // Offset of start of central directory
    uint16_t comment_len;         // ZIP file comment length
};
#pragma pack(pop)

constexpr uint32_t ZIP_LOCAL_SIG = 0x04034b50;
constexpr uint32_t ZIP_CENTRAL_SIG = 0x02014b50;
constexpr uint32_t ZIP_EOCD_SIG = 0x06054b50;
constexpr size_t CHUNK_SIZE = 1024 * 1024 * 8; // 8MB chunks for maximum speed

// Fast CRC32 calculation using zlib
uint32_t calculate_crc32(std::ifstream& file, uint64_t size) {
    uint32_t crc = crc32(0L, Z_NULL, 0);
    std::vector<uint8_t> buffer(CHUNK_SIZE);
    uint64_t remaining = size;
    
    while (remaining > 0) {
        size_t to_read = std::min<uint64_t>(remaining, CHUNK_SIZE);
        file.read(reinterpret_cast<char*>(buffer.data()), to_read);
        crc = crc32(crc, buffer.data(), to_read);
        remaining -= to_read;
    }
    
    return crc;
}

// Find End of Central Directory by scanning from end of file
bool find_eocd(std::ifstream& file, ZipEndOfCentralDir& eocd) {
    file.seekg(0, std::ios::end);
    std::streamoff file_size = file.tellg();
    
    // EOCD is at least 22 bytes, search last 64KB max
    std::streamoff search_start = std::max<std::streamoff>(0, file_size - 65536);
    file.seekg(search_start);
    
    std::vector<char> buffer(file_size - search_start);
    file.read(buffer.data(), buffer.size());
    
    // Search backwards for EOCD signature
    for (int i = buffer.size() - 22; i >= 0; --i) {
        uint32_t sig = *reinterpret_cast<uint32_t*>(&buffer[i]);
        if (sig == ZIP_EOCD_SIG) {
            memcpy(&eocd, &buffer[i], sizeof(ZipEndOfCentralDir));
            return true;
        }
    }
    
    return false;
}

// Converts ZIP archive directly to CFUP format with streaming
bool convert_zip_to_cfup(const std::string& zipFilePath, const std::string& cfupFilePath) {
    std::ifstream zip_in(zipFilePath, std::ios::binary);
    if (!zip_in) {
        Logger::Log(LOG_ERROR, "Failed to open ZIP file: " + zipFilePath);
        return false;
    }

    // Find and read End of Central Directory
    ZipEndOfCentralDir eocd;
    if (!find_eocd(zip_in, eocd)) {
        Logger::Log(LOG_ERROR, "Failed to find End of Central Directory");
        return false;
    }

    // Read Central Directory
    zip_in.seekg(eocd.cd_offset);
    std::vector<char> cd_buffer(eocd.cd_size);
    zip_in.read(cd_buffer.data(), eocd.cd_size);
    
    // Parse Central Directory entries
    struct FileEntry {
        std::string name;
        uint32_t local_offset;
        uint32_t compressed_size;
        uint32_t uncompressed_size;
        uint16_t compression;
        uint32_t crc32;
    };
    
    std::vector<FileEntry> files;
    size_t offset = 0;
    
    while (offset < eocd.cd_size) {
        ZipCentralDirHeader* cd = reinterpret_cast<ZipCentralDirHeader*>(&cd_buffer[offset]);
        if (cd->signature != ZIP_CENTRAL_SIG) break;
        
        std::string filename(cd_buffer.data() + offset + sizeof(ZipCentralDirHeader), cd->filename_len);
        
        // Skip directories
        if (filename.empty() || filename.back() != '/') {
            std::replace(filename.begin(), filename.end(), '\\', '/');
            files.push_back({
                filename,
                cd->local_header_offset,
                cd->compressed_size,
                cd->uncompressed_size,
                cd->compression,
                cd->crc32
            });
        }
        
        offset += sizeof(ZipCentralDirHeader) + cd->filename_len + cd->extra_len + cd->comment_len;
    }

    // Sort files alphabetically
    std::sort(files.begin(), files.end(), [](const FileEntry& a, const FileEntry& b) {
        return a.name < b.name;
    });

    uint32_t file_count = static_cast<uint32_t>(files.size());

    // Open output CFUP file
    std::ofstream out(cfupFilePath, std::ios::binary);
    if (!out) {
        Logger::Log(LOG_ERROR, "Failed to create CFUP file: " + cfupFilePath);
        return false;
    }

    // Write file count header
    out.write(reinterpret_cast<const char*>(&file_count), sizeof(file_count));

    std::vector<uint8_t> compressed_buf(CHUNK_SIZE);
    std::vector<uint8_t> decompressed_buf(CHUNK_SIZE);

    // Process each file with streaming decompression
    for (const auto& file : files) {
        uint32_t path_len = static_cast<uint32_t>(file.name.size());
        uint64_t data_size = file.uncompressed_size;

        // Write path length and path
        out.write(reinterpret_cast<const char*>(&path_len), sizeof(path_len));
        out.write(file.name.data(), path_len);

        // Write data size
        out.write(reinterpret_cast<const char*>(&data_size), sizeof(data_size));

        // Seek to local file header
        zip_in.seekg(file.local_offset);
        ZipLocalFileHeader local_hdr;
        zip_in.read(reinterpret_cast<char*>(&local_hdr), sizeof(local_hdr));
        
        // Skip filename and extra field
        zip_in.seekg(local_hdr.filename_len + local_hdr.extra_len, std::ios::cur);

        if (file.compression == 0) {
            // Stored (no compression) - direct copy
            uint32_t remaining = file.uncompressed_size;
            while (remaining > 0) {
                size_t to_read = std::min<uint32_t>(remaining, CHUNK_SIZE);
                zip_in.read(reinterpret_cast<char*>(compressed_buf.data()), to_read);
                out.write(reinterpret_cast<const char*>(compressed_buf.data()), to_read);
                remaining -= to_read;
            }
        } else if (file.compression == 8) {
            // DEFLATE compression - streaming decompression
            z_stream strm = {};
            strm.zalloc = Z_NULL;
            strm.zfree = Z_NULL;
            strm.opaque = Z_NULL;
            
            // Use raw deflate (negative window bits)
            if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) {
                Logger::Log(LOG_ERROR, "Failed to initialize decompression for: " + file.name);
                return false;
            }

            uint32_t compressed_remaining = file.compressed_size;
            
            while (compressed_remaining > 0 || strm.avail_out == 0) {
                if (strm.avail_in == 0 && compressed_remaining > 0) {
                    size_t to_read = std::min<uint32_t>(compressed_remaining, CHUNK_SIZE);
                    zip_in.read(reinterpret_cast<char*>(compressed_buf.data()), to_read);
                    strm.avail_in = to_read;
                    strm.next_in = compressed_buf.data();
                    compressed_remaining -= to_read;
                }

                strm.avail_out = CHUNK_SIZE;
                strm.next_out = decompressed_buf.data();
                
                int ret = inflate(&strm, Z_NO_FLUSH);
                if (ret != Z_OK && ret != Z_STREAM_END) {
                    Logger::Log(LOG_ERROR, "Decompression error for: " + file.name);
                    inflateEnd(&strm);
                    return false;
                }

                size_t decompressed = CHUNK_SIZE - strm.avail_out;
                if (decompressed > 0) {
                    out.write(reinterpret_cast<const char*>(decompressed_buf.data()), decompressed);
                }

                if (ret == Z_STREAM_END) break;
            }

            inflateEnd(&strm);
        } else {
            Logger::Log(LOG_ERROR, "Unsupported compression method for: " + file.name);
            return false;
        }
    }

    out.close();
    zip_in.close();

    Logger::Log(LOG_INFO, "Converted ZIP to CFUP: " + std::to_string(file_count) + " files");
    return true;
}

// Converts CFUP back to ZIP format with streaming compression
bool convert_cfup_to_zip(const std::string& cfupFilePath, const std::string& zipFilePath) {
    std::ifstream in(cfupFilePath, std::ios::binary);
    if (!in) {
        Logger::Log(LOG_ERROR, "Failed to open CFUP file: " + cfupFilePath);
        return false;
    }

    // Read file count
    uint32_t file_count;
    in.read(reinterpret_cast<char*>(&file_count), sizeof(file_count));
    if (in.fail()) {
        Logger::Log(LOG_ERROR, "Failed to read file count from CFUP");
        return false;
    }

    std::ofstream zip_out(zipFilePath, std::ios::binary);
    if (!zip_out) {
        Logger::Log(LOG_ERROR, "Failed to create ZIP file: " + zipFilePath);
        return false;
    }

    struct LocalFileInfo {
        std::string name;
        uint32_t offset;
        uint32_t compressed_size;
        uint32_t uncompressed_size;
        uint32_t crc32;
    };

    std::vector<LocalFileInfo> file_infos;
    std::vector<uint8_t> input_buf(CHUNK_SIZE);
    std::vector<uint8_t> output_buf(CHUNK_SIZE);

    // Write local file headers and compressed data
    for (uint32_t i = 0; i < file_count; ++i) {
        // Read path length and path
        uint32_t path_len;
        in.read(reinterpret_cast<char*>(&path_len), sizeof(path_len));
        
        std::string relative_path(path_len, '\0');
        in.read(&relative_path[0], path_len);

        // Read data size
        uint64_t data_size;
        in.read(reinterpret_cast<char*>(&data_size), sizeof(data_size));

        if (data_size > 0xFFFFFFFF) {
            Logger::Log(LOG_ERROR, "File too large for ZIP format: " + relative_path);
            return false;
        }

        uint32_t local_offset = static_cast<uint32_t>(zip_out.tellp());
        uint32_t uncompressed = static_cast<uint32_t>(data_size);

        // Calculate CRC32 for the uncompressed data
        std::streampos data_start = in.tellg();
        uint32_t crc = calculate_crc32(in, data_size);
        in.seekg(data_start); // Reset to start of data

        // Write local file header (will update sizes later)
        ZipLocalFileHeader local_hdr = {};
        local_hdr.signature = ZIP_LOCAL_SIG;
        local_hdr.version = 20;
        local_hdr.flags = 0;
        local_hdr.compression = 8; // DEFLATE
        local_hdr.crc32 = crc;
        local_hdr.uncompressed_size = uncompressed;
        local_hdr.filename_len = path_len;
        local_hdr.extra_len = 0;

        std::streampos hdr_pos = zip_out.tellp();
        zip_out.write(reinterpret_cast<const char*>(&local_hdr), sizeof(local_hdr));
        zip_out.write(relative_path.c_str(), path_len);

        // Streaming compression
        z_stream strm = {};
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        
        if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
            Logger::Log(LOG_ERROR, "Failed to initialize compression for: " + relative_path);
            return false;
        }

        uint64_t remaining = data_size;
        uint32_t compressed_total = 0;

        while (remaining > 0) {
            size_t to_read = std::min<uint64_t>(remaining, CHUNK_SIZE);
            in.read(reinterpret_cast<char*>(input_buf.data()), to_read);
            
            strm.avail_in = to_read;
            strm.next_in = input_buf.data();
            remaining -= to_read;

            int flush = (remaining == 0) ? Z_FINISH : Z_NO_FLUSH;

            do {
                strm.avail_out = CHUNK_SIZE;
                strm.next_out = output_buf.data();
                
                deflate(&strm, flush);
                
                size_t compressed = CHUNK_SIZE - strm.avail_out;
                if (compressed > 0) {
                    zip_out.write(reinterpret_cast<const char*>(output_buf.data()), compressed);
                    compressed_total += compressed;
                }
            } while (strm.avail_out == 0);
        }

        deflateEnd(&strm);

        // Update compressed size in header
        std::streampos end_pos = zip_out.tellp();
        zip_out.seekp(hdr_pos);
        local_hdr.compressed_size = compressed_total;
        zip_out.write(reinterpret_cast<const char*>(&local_hdr), sizeof(local_hdr));
        zip_out.seekp(end_pos);

        file_infos.push_back({relative_path, local_offset, compressed_total, uncompressed, crc});
    }

    // Write Central Directory
    uint32_t cd_offset = static_cast<uint32_t>(zip_out.tellp());
    
    for (const auto& info : file_infos) {
        ZipCentralDirHeader cd = {};
        cd.signature = ZIP_CENTRAL_SIG;
        cd.version_made = 20;
        cd.version_needed = 20;
        cd.flags = 0;
        cd.compression = 8;
        cd.crc32 = info.crc32;
        cd.compressed_size = info.compressed_size;
        cd.uncompressed_size = info.uncompressed_size;
        cd.filename_len = info.name.size();
        cd.local_header_offset = info.offset;
        
        zip_out.write(reinterpret_cast<const char*>(&cd), sizeof(cd));
        zip_out.write(info.name.c_str(), info.name.size());
    }

    uint32_t cd_size = static_cast<uint32_t>(zip_out.tellp()) - cd_offset;

    // Write End of Central Directory
    ZipEndOfCentralDir eocd = {};
    eocd.signature = ZIP_EOCD_SIG;
    eocd.cd_records_disk = file_count;
    eocd.cd_records_total = file_count;
    eocd.cd_size = cd_size;
    eocd.cd_offset = cd_offset;
    
    zip_out.write(reinterpret_cast<const char*>(&eocd), sizeof(eocd));

    zip_out.close();
    in.close();

    Logger::Log(LOG_INFO, "Converted CFUP to ZIP: " + std::to_string(file_count) + " files");
    return true;
}

#endif // ZIP_TO_CFUP_CONVERTER_H