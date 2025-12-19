#ifndef FILESERVER_H
#define FILESERVER_H

#include "httplib.h"
#include <string>
#include <filesystem>
#include <mutex>

namespace fs = std::filesystem;

// --- UTILS ---
void log(const std::string& msg);
std::string urlEncode(const std::string& str);
std::string htmlEncode(const std::string& data);
std::string formatSize(uintmax_t size);
std::string getMimeType(const std::string& path);
std::string getPlayerHTML(const std::string& filename, const std::string& rawUrl);

// --- FILE SERVER CLASS ---
/**
 * @brief High-performance file server class using cpp-httplib.
 * Handles directory listing, file streaming with range support, and media playback.
 */
class FileServer {
public:
    /**
     * @param p Port to listen on.
     * @param path Local directory path to serve files from.
     */
    FileServer(int p, const std::string& path);
    
    /**
     * @brief Starts the server. This call is blocking.
     */
    void start();

    /**
     * @brief Stops the server if it's running.
     */
    void stop();

private:
    std::string basePath;
    int port;
    httplib::Server server;

    bool isPathSafe(const fs::path& requested, const fs::path& base);
    void setupRoutes();
    void handleRequest(const httplib::Request& req, httplib::Response& res);
    void serveDirectory(const fs::path& fullPath, const std::string& urlPath, httplib::Response& res);
    void serveFile(const fs::path& fullPath, const httplib::Request& req, 
                   httplib::Response& res, bool isRaw, bool isDownload);
};

#endif // FILESERVER_H