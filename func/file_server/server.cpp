// server.cpp - Implementation of FileServer class
#include "server.h"
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <ctime>

// --- CONFIGURATION ---
const size_t CHUNK_SIZE = 1024 * 1024; // 1MB chunks for streaming

// --- ICONS (SVG) ---
const std::string ICON_FOLDER = R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="#fbbf24" class="icon"><path d="M19.5 21a3 3 0 0 0 3-3v-4.5a3 3 0 0 0-3-3h-15a3 3 0 0 0-3 3V18a3 3 0 0 0 3 3h15ZM1.5 10.146V6a3 3 0 0 1 3-3h5.379a2.25 2.25 0 0 1 1.59.659l2.122 2.121c.14.141.331.22.53.22H19.5a3 3 0 0 1 3 3v1.146A4.483 4.483 0 0 0 19.5 9h-15a4.483 4.483 0 0 0-3 1.146Z"/></svg>)";
const std::string ICON_FILE = R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="#94a3b8" class="icon"><path fill-rule="evenodd" d="M4.5 5.653c0-1.426 1.529-2.33 2.779-1.643l11.54 6.348c1.295.712 1.295 2.573 0 3.285L7.28 19.991c-1.25.687-2.779-.217-2.779-1.643V5.653Z" clip-rule="evenodd"/></svg>)";
const std::string ICON_DOC = R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="#94a3b8" class="icon"><path fill-rule="evenodd" d="M5.625 1.5c-1.036 0-1.875.84-1.875 1.875v17.25c0 1.035.84 1.875 1.875 1.875h12.75c1.035 0 1.875-.84 1.875-1.875V12.75A3.75 3.75 0 0 0 16.5 9h-1.875a1.875 1.875 0 0 1-1.875-1.875V5.25A3.75 3.75 0 0 0 9 1.5H5.625ZM7.5 15a.75.75 0 0 1 .75-.75h7.5a.75.75 0 0 1 0 1.5h-7.5A.75.75 0 0 1 7.5 15Zm.75 2.25a.75.75 0 0 0 0 1.5H12a.75.75 0 0 0 0-1.5H8.25Z" clip-rule="evenodd"/><path d="M12.971 1.816A5.23 5.23 0 0 1 14.25 5.25v1.875c0 .207.168.375.375.375H16.5a5.23 5.23 0 0 1 3.434 1.279 9.768 9.768 0 0 0-6.963-6.963Z"/></svg>)";
const std::string ICON_DOWNLOAD = R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="currentColor" class="icon-sm"><path fill-rule="evenodd" d="M12 2.25a.75.75 0 0 1 .75.75v11.69l3.22-3.22a.75.75 0 1 1 1.06 1.06l-4.5 4.5a.75.75 0 0 1-1.06 0l-4.5-4.5a.75.75 0 1 1 1.06-1.06l3.22 3.22V3a.75.75 0 0 1 .75-.75Zm-9 13.5a.75.75 0 0 1 .75.75v2.25a1.5 1.5 0 0 0 1.5 1.5h13.5a1.5 1.5 0 0 0 1.5-1.5V16.5a.75.75 0 0 1 1.5 0v2.25a3 3 0 0 1-3 3H5.25a3 3 0 0 1-3-3V16.5a.75.75 0 0 1 .75-.75Z" clip-rule="evenodd"/></svg>)";
const std::string ICON_BACK = R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="currentColor" class="icon"><path fill-rule="evenodd" d="M9.53 2.47a.75.75 0 0 1 0 1.06L4.81 8.25H15a6.75 6.75 0 0 1 0 13.5h-3a.75.75 0 0 1 0-1.5h3a5.25 5.25 0 1 0 0-10.5H4.81l4.72 4.72a.75.75 0 1 1-1.06 1.06l-6-6a.75.75 0 0 1 0-1.06l6-6a.75.75 0 0 1 1.06 0Z" clip-rule="evenodd"/></svg>)";

// --- UTILS ---
static std::mutex logMutex;

void log(const std::string& msg) {
    std::lock_guard<std::mutex> lock(logMutex);
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    std::cout << "\033[1;32m[" << std::put_time(&tm, "%H:%M:%S") << "]\033[0m " << msg << std::endl;
}

std::string urlEncode(const std::string& str) {
    std::ostringstream esc;
    esc.fill('0'); esc << std::hex;
    for (unsigned char c : str) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') 
            esc << c;
        else 
            esc << '%' << std::setw(2) << int(c);
    }
    return esc.str();
}

std::string htmlEncode(const std::string& data) {
    std::string buf;
    buf.reserve(data.size());
    for (char c : data) {
        switch(c) {
            case '&': buf.append("&amp;"); break;
            case '\"': buf.append("&quot;"); break;
            case '\'': buf.append("&apos;"); break;
            case '<': buf.append("&lt;"); break;
            case '>': buf.append("&gt;"); break;
            default: buf.push_back(c); break;
        }
    }
    return buf;
}

std::string formatSize(uintmax_t size) {
    const char* suffix[] = {"B", "KB", "MB", "GB", "TB"};
    int i = 0;
    double dblBytes = static_cast<double>(size);
    while (dblBytes > 1024 && i < 4) { 
        dblBytes /= 1024.0; 
        i++; 
    }
    std::ostringstream out; 
    out << std::fixed << std::setprecision(2) << dblBytes << " " << suffix[i];
    return out.str();
}

std::string getMimeType(const std::string& path) {
    static const std::map<std::string, std::string> mime = {
        {".html", "text/html"}, {".css", "text/css"}, {".js", "application/javascript"},
        {".json", "application/json"}, {".png", "image/png"}, {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"}, {".gif", "image/gif"}, {".svg", "image/svg+xml"},
        {".mp4", "video/mp4"}, {".webm", "video/webm"}, {".mkv", "video/x-matroska"},
        {".avi", "video/x-msvideo"}, {".mov", "video/quicktime"}, {".mp3", "audio/mpeg"},
        {".wav", "audio/wav"}, {".ogg", "audio/ogg"}, {".pdf", "application/pdf"},
        {".txt", "text/plain"}, {".zip", "application/zip"}, {".tar", "application/x-tar"},
        {".gz", "application/gzip"}
    };
    auto ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return mime.count(ext) ? mime.at(ext) : "application/octet-stream";
}

std::string getPlayerHTML(const std::string& filename, const std::string& rawUrl) {
    std::string html = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
    html += "<title>" + htmlEncode(filename) + "</title>\n";
    html += "<script src=\"https://unpkg.com/@ffmpeg/ffmpeg@0.12.7/dist/umd/ffmpeg.umd.js\"></script>\n";
    html += "<script src=\"https://unpkg.com/@ffmpeg/util@0.12.1/dist/umd/index.js\"></script>\n";
    html += "<style>\n";
    html += "body{margin:0;background:#000;color:#fff;min-height:100vh;display:flex;flex-direction:column;align-items:center;justify-content:center;font-family:system-ui,sans-serif}\n";
    html += "video{max-width:100%;max-height:80vh;box-shadow:0 0 20px rgba(0,0,0,0.5);outline:none}\n";
    html += ".header{width:100%;padding:15px;text-align:center;position:absolute;top:0;background:linear-gradient(to bottom, rgba(0,0,0,0.8), transparent);z-index:10}\n";
    html += ".loader{position:absolute;inset:0;background:rgba(0,0,0,0.9);z-index:20;display:none;flex-direction:column;align-items:center;justify-content:center;text-align:center;padding:20px}\n";
    html += ".spinner{width:40px;height:40px;border:4px solid rgba(255,255,255,0.1);border-top-color:#6366f1;border-radius:50%;animation:spin 0.8s linear infinite;margin-bottom:20px}\n";
    html += "@keyframes spin{to{transform:rotate(360deg)}}\n";
    html += ".btn{display:inline-block;margin-top:20px;padding:8px 16px;background:#6366f1;color:white;text-decoration:none;border-radius:4px;font-size:0.9rem;}\n";
    html += "</style></head><body>\n";
    html += "<div class=\"header\"><h3>" + htmlEncode(filename) + "</h3></div>\n";
    html += "<div class=\"loader\" id=\"loader\"><div class=\"spinner\"></div><div class=\"msg\">Transcoding...</div></div>\n";
    html += "<video id=\"vid\" controls playsinline autoplay src=\"" + rawUrl + "\" crossorigin=\"anonymous\"></video>\n";
    html += "<script>\n";
    html += "const vid=document.getElementById('vid'),loader=document.getElementById('loader');\n";
    html += "vid.onerror=async()=>{\n";
    html += "    if(vid.error?.code===4){\n";
    html += "        console.log('Native fail, trying FFmpeg');loader.style.display='flex';\n";
    html += "        const {FFmpeg}=window.FFmpegWASM,{fetchFile}=window.FFmpegUtil,ffmpeg=new FFmpeg();\n";
    html += "        try{\n";
    html += "            await ffmpeg.load();\n";
    html += "            await ffmpeg.writeFile('input', await fetchFile('" + rawUrl + "'));\n";
    html += "            await ffmpeg.exec(['-i','input','-c:v','libx264','-preset','ultrafast','-crf','28','-c:a','aac','output.mp4']);\n";
    html += "            const data=await ffmpeg.readFile('output.mp4');\n";
    html += "            vid.src=URL.createObjectURL(new Blob([data.buffer],{type:'video/mp4'}));\n";
    html += "            loader.style.display='none';vid.play();\n";
    html += "        }catch(e){loader.innerHTML='Error: '+e.message;}\n";
    html += "    }\n";
    html += "};\n";
    html += "</script></body></html>";
    return html;
}

// --- FILE SERVER CLASS IMPLEMENTATION ---

FileServer::FileServer(int p, const std::string& path) 
    : basePath(path), port(p) {  // Fixed order to match declaration order
    if (!fs::exists(basePath)) throw std::runtime_error("Path does not exist");
    if (!fs::is_directory(basePath)) throw std::runtime_error("Not a directory");
    basePath = fs::canonical(basePath).string();
}

bool FileServer::isPathSafe(const fs::path& requested, const fs::path& base) {
    try {
        auto canonical_base = fs::canonical(base);
        auto canonical_req = fs::canonical(requested);
        auto mismatch_pair = std::mismatch(canonical_base.begin(), canonical_base.end(), 
                                           canonical_req.begin(), canonical_req.end());
        return mismatch_pair.first == canonical_base.end();
    } catch(...) {
        return false;
    }
}

void FileServer::setupRoutes() {
    server.set_payload_max_length(1024 * 1024 * 100); 
    
    server.set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"X-Content-Type-Options", "nosniff"},
        {"X-Frame-Options", "SAMEORIGIN"}
    });

    server.Get(R"(.*)", [this](const httplib::Request& req, httplib::Response& res) {
        handleRequest(req, res);
    });

    server.set_error_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_content("<html><body><h1>Error " + std::to_string(res.status) + "</h1></body></html>", "text/html");
    });

    server.set_logger([](const httplib::Request& req, const httplib::Response& res) {
        log(req.method + " " + req.path + " -> " + std::to_string(res.status));
    });
}

void FileServer::handleRequest(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string path = req.path;
        if (path.empty() || path[0] != '/') path = "/" + path;

        fs::path fullPath = fs::path(basePath) / path.substr(1);
        
        if (!fs::exists(fullPath)) {
            res.status = 404;
            res.set_content("<html><body><h1>404 Not Found</h1></body></html>", "text/html");
            return;
        }

        if (!isPathSafe(fullPath, basePath)) {
            res.status = 403;
            res.set_content("<html><body><h1>403 Forbidden</h1></body></html>", "text/html");
            return;
        }

        bool isRaw = req.has_param("raw");
        bool isDownload = req.has_param("download");

        if (fs::is_directory(fullPath)) {
            serveDirectory(fullPath, path, res);
        } else {
            serveFile(fullPath, req, res, isRaw, isDownload);
        }
    } catch (const std::exception& e) {
        log("Error: " + std::string(e.what()));
        res.status = 500;
        res.set_content("<html><body><h1>500 Internal Server Error</h1></body></html>", "text/html");
    }
}

void FileServer::serveDirectory(const fs::path& fullPath, const std::string& urlPath, httplib::Response& res) {
    std::string body = R"(<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Files</title>
<style>
:root{--bg:#0f172a;--card:#1e293b;--text:#e2e8f0;--sub:#94a3b8;--accent:#6366f1;--hover:#334155;--border:#334155}
*{box-sizing:border-box}
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;background:var(--bg);color:var(--text);margin:0;padding:20px;font-size:15px;line-height:1.5}
a{text-decoration:none;color:inherit;transition:color 0.2s}
.dir-list{display:flex;flex-direction:column;gap:8px;margin-bottom:24px}
.file-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:16px}
.item{display:flex;align-items:center;background:var(--card);padding:10px 14px;border-radius:8px;border:1px solid transparent;transition:all 0.2s}
.item:hover{background:var(--hover);border-color:var(--accent);transform:translateY(-1px)}
.icon{width:20px;height:20px;margin-right:10px;flex-shrink:0}
.icon-sm{width:18px;height:18px}
.card{display:flex;flex-direction:column;align-items:center;justify-content:center;background:var(--card);padding:24px 16px;border-radius:12px;border:1px solid transparent;transition:all 0.2s;text-align:center;aspect-ratio:1}
.card:hover{background:var(--hover);border-color:var(--accent);transform:translateY(-4px);box-shadow:0 10px 20px -5px rgba(0,0,0,0.3)}
.card .icon{width:48px;height:48px;margin:0 0 16px 0;opacity:0.8}
.card .name{font-weight:500;font-size:0.95rem;margin-bottom:8px;word-break:break-word;line-height:1.4;display:-webkit-box;-webkit-line-clamp:2;-webkit-box-orient:vertical;overflow:hidden;width:100%}
.card .meta{color:var(--sub);font-size:0.8rem}
.name{flex-grow:1;font-weight:500;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;margin-right:16px}
.meta{display:flex;align-items:center;gap:16px;color:var(--sub);font-size:0.85rem;flex-shrink:0}
@media(max-width:600px){.meta{display:none}.name{margin-right:0}}
</style></head><body>
<div class="container">
<div class="header">
    <div class="path">)" + htmlEncode(urlPath) + R"(</div>
</div>

<div class="dir-list">)";

    if (urlPath != "/") {
        body += "<a class='item' href='../'>" + ICON_BACK + "<div class='name'>..</div></a>";
    }

    std::vector<fs::directory_entry> dirs, files;
    try {
        for (const auto& entry : fs::directory_iterator(fullPath)) {
            if (entry.is_directory()) dirs.push_back(entry);
            else files.push_back(entry);
        }
    } catch(...) {}
    
    auto sortFn = [](const auto& a, const auto& b) {
        return a.path().filename().string() < b.path().filename().string();
    };
    std::sort(dirs.begin(), dirs.end(), sortFn);
    std::sort(files.begin(), files.end(), sortFn);

    for (const auto& entry : dirs) {
        std::string name = entry.path().filename().string();
        if (name.size() > 0 && name[0] == '.') continue; 
        
        std::string link = urlPath + (urlPath.back() == '/' ? "" : "/") + urlEncode(name);
        body += "<a class='item' href='" + link + "'>";
        body += ICON_FOLDER;
        body += "<div class='name'>" + htmlEncode(name) + "</div></a>";
    }
    body += "</div>";

    body += "<div class='file-grid'>";
    for (const auto& entry : files) {
        std::string name = entry.path().filename().string();
        if (name.size() > 0 && name[0] == '.') continue; 
        
        std::string link = urlPath + (urlPath.back() == '/' ? "" : "/") + urlEncode(name);
        uintmax_t size = 0;
        try { size = entry.file_size(); } catch(...) {}
        
        body += "<a class='card' href='" + link + "'>";
        body += ICON_DOC;
        body += "<div class='name'>" + htmlEncode(name) + "</div>";
        body += "<div class='meta'>" + formatSize(size) + "</div>";
        body += "</a>";
    }
    
    body += "</div></div></body></html>";
    res.set_content(body, "text/html; charset=utf-8");
}

void FileServer::serveFile(const fs::path& fullPath, const httplib::Request& req, 
               httplib::Response& res, bool isRaw, bool isDownload) {
    
    std::string ext = fullPath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    bool isVideo = (ext == ".mp4" || ext == ".mkv" || ext == ".avi" || 
                   ext == ".webm" || ext == ".mov");
    
    if (!isRaw && !isDownload && isVideo) {
        std::string html = getPlayerHTML(fullPath.filename().string(), 
                                        req.path + "?raw=1");
        res.set_content(html, "text/html; charset=utf-8");
        return;
    }

    std::string mimeType = isDownload ? "application/octet-stream" : getMimeType(fullPath.string());
    
    if (isDownload) {
        res.set_header("Content-Disposition", 
                      "attachment; filename=\"" + fullPath.filename().string() + "\"");
    }

    res.set_content_provider(
        fs::file_size(fullPath),
        mimeType,
        [fullPath](size_t offset, size_t length, httplib::DataSink& sink) {
            std::ifstream file(fullPath, std::ios::binary);
            if (!file) return false;
            
            file.seekg(offset);
            std::vector<char> buffer(std::min(length, CHUNK_SIZE));
            
            size_t remaining = length;
            while (remaining > 0 && file) {
                size_t toRead = std::min(remaining, CHUNK_SIZE);
                file.read(buffer.data(), toRead);
                size_t bytesRead = file.gcount();
                if (bytesRead == 0) break;
                if (!sink.write(buffer.data(), bytesRead)) return false; 
                remaining -= bytesRead;
            }
            return true;
        }
    );
}

void FileServer::start() {
    setupRoutes();
    log("Server running on port " + std::to_string(port));
    log("Serving: " + basePath);
    if (!server.listen("0.0.0.0", port)) {
        throw std::runtime_error("Bind failed");
    }
}

void FileServer::stop() {
    server.stop();
}