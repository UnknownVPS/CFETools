#include "server.h"
#include "httplib.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <vector>
#include <iostream>
#include <memory>
#include <array>

namespace fs = std::filesystem;

namespace {
    std::string html_escape(const std::string& s) {
        std::string out;
        for (char c : s) {
            switch (c) {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                case '"': out += "&quot;"; break;
                case '\'': out += "&#39;"; break; 
                default: out += c;
            }
        }
        return out;
    }

    std::string format_size(uintmax_t bytes) {
        if (bytes == 0) return "0 B";
        const char* suffixes[] = {"B", "KB", "MB", "GB", "TB"};
        int s = 0;
        double count = bytes;
        while (count >= 1024 && s < 4) {
            s++;
            count /= 1024;
        }
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << count << " " << suffixes[s];
        return ss.str();
    }

    void serve_directory(const fs::path& dir_path, const std::string& url_path, httplib::Response& res) {
        std::string html = R"===(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>File Server</title>
</head>
<body style="font-family: monospace; max-width: 900px; margin: 20px auto;">
    <h1>Index of <span id="currentPathDisplay">{{PATH_DISPLAY}}</span></h1>
    <div style="margin-bottom: 20px;">
        {{UP_BUTTON}}
        <button onclick="window.location.reload()">Refresh</button>
    </div>
    <table border="1" cellpadding="10" cellspacing="0" style="width: 100%; border-collapse: collapse;">
        <thead style="background-color: #efefef;">
            <tr>
                <th style="text-align: left;">Name</th>
                <th style="width: 100px;">Type</th>
                <th style="width: 100px;">Size</th>
                <th style="width: 100px;">Action</th>
            </tr>
        </thead>
        <tbody id="fileList">
            {{FILE_ROWS}}
        </tbody>
    </table>
</body>
</html>
)===";

        std::vector<fs::directory_entry> entries;
        // Check directory iterator errors explicitly
        std::error_code dir_ec;
        for (auto& entry : fs::directory_iterator(dir_path, dir_ec)) {
            entries.push_back(entry);
        }

        std::sort(entries.begin(), entries.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
            bool a_is_dir = a.is_directory();
            bool b_is_dir = b.is_directory();
            if (a_is_dir == b_is_dir) {
                return a.path().filename().string() < b.path().filename().string();
            }
            return a_is_dir;
        });

        std::ostringstream rows;
        
        if (entries.empty()) {
            rows << "<tr><td colspan='4' style='text-align:center; color:gray;'>Empty Directory</td></tr>";
        } else {
            for (auto& entry : entries) {
                std::string name = html_escape(entry.path().filename().string());
                std::string link = url_path + name; 
                
                bool isDir = entry.is_directory();
                if (isDir) link += "/";

                std::string sizeStr = isDir ? "-" : format_size(entry.file_size());

                rows << "<tr>";
                if (isDir) {
                    rows << "<td><a href=\"" << link << "\"><b>" << name << "/</b></a></td>";
                } else {
                    rows << "<td><a href=\"" << link << "\">" << name << "</a></td>";
                }
                rows << "<td>" << (isDir ? "DIR" : "FILE") << "</td>";
                rows << "<td>" << sizeStr << "</td>";
                if (isDir) {
                    rows << "<td><button onclick=\"window.location.href='" << link << "'\">Open</button></td>";
                } else {
                    rows << "<td><a href=\"" << link << "\" download>Download</a></td>";
                }
                rows << "</tr>";
            }
        }

        std::string upBtnHtml;
        if (url_path == "/") {
            upBtnHtml = "<button disabled>.. (Parent Directory)</button>";
        } else {
            std::string parentPath = url_path;
            if (!parentPath.empty() && parentPath.back() == '/') parentPath.pop_back();
            size_t lastSlash = parentPath.find_last_of('/');
            if (lastSlash != std::string::npos) {
                parentPath = parentPath.substr(0, lastSlash + 1);
            } else {
                parentPath = "/"; 
            }
            upBtnHtml = "<button onclick=\"window.location.href='" + parentPath + "'\">.. (Parent Directory)</button>";
        }

        std::string pathDisplay = (url_path == "/") ? "/" : url_path;
        
        auto replace_all = [](std::string& str, const std::string& from, const std::string& to) {
            if (from.empty()) return;
            size_t start_pos = 0;
            while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
                str.replace(start_pos, from.length(), to);
                start_pos += to.length();
            }
        };

        replace_all(html, "{{PATH_DISPLAY}}", pathDisplay);
        replace_all(html, "{{UP_BUTTON}}", upBtnHtml);
        replace_all(html, "{{FILE_ROWS}}", rows.str());

        res.set_content(html, "text/html");
    }
}

int start_server(const std::string& root_path, int port) {
    httplib::Server svr;

    // OPTIMIZATION: Resolve the real root path ONCE at startup.
    // Running fs::canonical on every request adds I/O latency.
    std::error_code ec;
    fs::path real_root = fs::canonical(root_path, ec);
    if (ec) {
        real_root = fs::absolute(root_path).lexically_normal();
    } else {
        real_root = real_root.lexically_normal();
    }

    if (!fs::exists(real_root) || !fs::is_directory(real_root)) {
        std::cerr << "Error: Directory does not exist: " << real_root << std::endl;
        return 1;
    }

    // Helper lambda for path checking, now using the pre-calculated real_root
    auto resolve_safe_path = [&real_root](const std::string& url_path) -> std::string {
        std::string rel_url = url_path;
        if (!rel_url.empty() && rel_url[0] == '/')
            rel_url.erase(0, 1);

        // Combine paths and normalize (removes .. and . segments)
        fs::path combined = (real_root / fs::path(rel_url)).lexically_normal();

        // Check if the combined path starts with the real_root path
        // We use mismatch to prevent string comparison issues on Windows/Unix separators
        auto [mismatch_root, mismatch_combined] = std::mismatch(
            real_root.begin(), real_root.end(),
            combined.begin(), combined.end()
        );

        if (mismatch_root != real_root.end())
            return ""; // Path traversal attempt detected

        return combined.string();
    };

    svr.Get(R"(/.*)", [&](const httplib::Request& req, httplib::Response& res) {
        std::string url_path = req.path;
        std::string fs_path = resolve_safe_path(url_path);

        if (fs_path.empty()) {
            res.status = 403;
            res.set_content("Forbidden", "text/plain");
            return;
        }

        fs::path p = fs::path(fs_path);

        if (!fs::exists(p)) {
            res.status = 404;
            res.set_content("Not Found", "text/plain");
            return;
        }

        if (fs::is_directory(p)) {
            if (url_path.back() != '/') {
                res.set_redirect(url_path + "/");
                return;
            }
            serve_directory(p, url_path, res);
        } else {
            std::error_code size_ec;
            uintmax_t file_size = fs::file_size(p, size_ec);
            if (size_ec) {
                res.status = 500;
                res.set_content("Could not determine file size", "text/plain");
                return;
            }

            // 1. Parse Range Header
            std::string range_header = req.get_header_value("Range");
            uintmax_t start = 0;
            uintmax_t end = file_size - 1;
            bool is_range = false;

            if (!range_header.empty()) {
                if (range_header.find("bytes=") == 0) {
                    std::string range_spec = range_header.substr(6);
                    size_t dash_pos = range_spec.find('-');
                    if (dash_pos != std::string::npos) {
                        try {
                            std::string s_start = range_spec.substr(0, dash_pos);
                            std::string s_end = range_spec.substr(dash_pos + 1);
                            if (!s_start.empty()) start = std::stoull(s_start);
                            if (!s_end.empty()) end = std::stoull(s_end);
                            else end = file_size - 1;
                            if (start < file_size && end < file_size && start <= end) {
                                is_range = true;
                            }
                        } catch (...) { /* Ignore invalid range */ }
                    }
                }
            }

            uintmax_t content_length = is_range ? (end - start + 1) : file_size;

            // 2. Open File
            auto file_ptr = std::make_shared<std::ifstream>(p, std::ios::binary);
            if (!file_ptr->is_open()) {
                res.status = 500;
                res.set_content("Failed to open file", "text/plain");
                return;
            }

            // 3. Set Headers
            if (is_range) {
                res.status = 206;
                std::string content_range = "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" + std::to_string(file_size);
                res.set_header("Content-Range", content_range.c_str());
            } else {
                res.status = 200;
                res.set_header("Accept-Ranges", "bytes");
            }

            // 4. Stream
            res.set_content_provider(
                content_length,
                "application/octet-stream",
                [file_ptr, start](size_t offset, size_t length, httplib::DataSink &sink) {
                    uintmax_t file_pos = start + offset;

                    file_ptr->clear();
                    file_ptr->seekg(file_pos);
                    
                    if (!file_ptr->good()) return false;

                    const size_t chunk_size = 64 * 1024; 
                    std::array<char, chunk_size> buffer; 

                    size_t to_read = std::min(length, chunk_size);
                    
                    file_ptr->read(buffer.data(), to_read);
                    size_t read_count = file_ptr->gcount();

                    if (read_count > 0) {
                        sink.write(buffer.data(), read_count);
                    }
                    
                    return true;
                }
            );
        }
    });

    std::cout << "Server running at http://0.0.0.0:" << port << "\n";
    std::cout << "Serving directory: " << real_root << "\n";
    
    if (!svr.listen("0.0.0.0", port)) {
        std::cerr << "Error: Failed to start server on port " << port << std::endl;
        return 1;
    }

    return 0;
}