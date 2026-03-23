#include "backup.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <stdexcept>

#include "../../utils/logger/logger.h"
#include "../../utils/hashers/fileHasher.hpp"
#include "../../func/folder_packer/folder_packer.h"
#include "../../func/patch_creation/patch.h"

namespace fs = std::filesystem;

// ============================================================
// Internal constants
// ============================================================
static constexpr const char* MANIFEST_FILENAME  = "manifest.json";
static constexpr const char* BASELINE_FILENAME  = "baseline.cfup";
static constexpr const char* PATCHES_SUBDIR     = "patches";
static constexpr const char* STAGING_SUBDIR     = "rebase_staging";

// ============================================================
// Minimal JSON helpers (reuses existing json.h style but
// extended for arrays/nested objects we need here)
// ============================================================

// Escape a string for JSON output
static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;
        }
    }
    return out;
}

// Read a string value for a given key from a flat JSON line
// Only handles simple "key": "value" and "key": number patterns
static std::string json_read_string(const std::string& line, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = line.find(search);
    if (pos == std::string::npos) return "";

    auto colon = line.find(':', pos + search.size());
    if (colon == std::string::npos) return "";

    // Skip whitespace after colon
    auto vstart = line.find_first_not_of(" \t", colon + 1);
    if (vstart == std::string::npos) return "";

    if (line[vstart] == '"') {
        auto vend = line.find('"', vstart + 1);
        if (vend == std::string::npos) return "";
        return line.substr(vstart + 1, vend - vstart - 1);
    }
    // numeric or boolean — read until comma, } or end
    auto vend = line.find_first_of(",}", vstart);
    std::string raw = (vend == std::string::npos)
                    ? line.substr(vstart)
                    : line.substr(vstart, vend - vstart);
    // trim trailing whitespace
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t')) raw.pop_back();
    return raw;
}

// ============================================================
// Manifest serialisation
// ============================================================

bool write_manifest(const std::string& store_path, const BackupManifest& m) {
    fs::path mpath = fs::path(store_path) / MANIFEST_FILENAME;
    std::ofstream f(mpath);
    if (!f) {
        Logger::Log(LOG_ERROR, "Cannot write manifest: " + mpath.string());
        return false;
    }

    f << "{\n";
    f << "  \"store_version\": "   << m.store_version    << ",\n";
    f << "  \"baseline_id\": \""   << json_escape(m.baseline_id)   << "\",\n";
    f << "  \"baseline_file\": \"" << json_escape(m.baseline_file) << "\",\n";
    f << "  \"baseline_hash\": \"" << json_escape(m.baseline_hash) << "\",\n";
    f << "  \"rebase_threshold\": " << m.rebase_threshold << ",\n";
    f << "  \"anchor_interval\": "  << m.anchor_interval  << ",\n";

    // versions array
    f << "  \"versions\": [\n";
    for (size_t i = 0; i < m.versions.size(); ++i) {
        const auto& v = m.versions[i];
        f << "    {\n";
        f << "      \"id\": \""                    << json_escape(v.id)                    << "\",\n";
        f << "      \"label\": \""                 << json_escape(v.label)                 << "\",\n";
        f << "      \"timestamp\": "               << v.timestamp                          << ",\n";
        f << "      \"patch_file\": \""            << json_escape(v.patch_file)            << "\",\n";
        f << "      \"patch_size\": "              << v.patch_size                         << ",\n";
        f << "      \"patch_hash\": \""            << json_escape(v.patch_hash)            << "\",\n";
        f << "      \"relative_to_baseline\": \""  << json_escape(v.relative_to_baseline)  << "\"\n";
        f << "    }";
        if (i + 1 < m.versions.size()) f << ",";
        f << "\n";
    }
    f << "  ],\n";

    // rebase_history array
    f << "  \"rebase_history\": [\n";
    for (size_t i = 0; i < m.rebase_history.size(); ++i) {
        const auto& r = m.rebase_history[i];
        f << "    {\n";
        f << "      \"from_baseline\": \""        << json_escape(r.from_baseline)      << "\",\n";
        f << "      \"to_baseline\": \""           << json_escape(r.to_baseline)        << "\",\n";
        f << "      \"timestamp\": "               << r.timestamp                       << ",\n";
        f << "      \"versions_reexpressed\": "    << r.versions_reexpressed            << "\n";
        f << "    }";
        if (i + 1 < m.rebase_history.size()) f << ",";
        f << "\n";
    }
    f << "  ]\n";
    f << "}\n";

    return f.good();
}

bool read_manifest(const std::string& store_path, BackupManifest& out) {
    fs::path mpath = fs::path(store_path) / MANIFEST_FILENAME;
    std::ifstream f(mpath);
    if (!f) {
        Logger::Log(LOG_ERROR, "Cannot read manifest: " + mpath.string());
        return false;
    }

    out = BackupManifest{};

    // State machine parser — handles the flat fields plus the two arrays
    enum class Section { ROOT, VERSIONS, REBASE_HISTORY };
    Section section = Section::ROOT;

    BackupVersion  cur_ver;
    RebaseRecord   cur_rec;
    bool           in_object = false;

    std::string line;
    while (std::getline(f, line)) {
        // Detect section transitions
        if (line.find("\"versions\"") != std::string::npos &&
            line.find('[') != std::string::npos) {
            section = Section::VERSIONS;
            continue;
        }
        if (line.find("\"rebase_history\"") != std::string::npos &&
            line.find('[') != std::string::npos) {
            section = Section::REBASE_HISTORY;
            continue;
        }

        if (section == Section::ROOT) {
            // Parse flat root fields
            auto sv = [&](const std::string& k) { return json_read_string(line, k); };

            if (line.find("\"store_version\"") != std::string::npos)
                out.store_version = std::stoi(sv("store_version").empty() ? "1" : sv("store_version"));
            else if (line.find("\"baseline_id\"") != std::string::npos)
                out.baseline_id = sv("baseline_id");
            else if (line.find("\"baseline_file\"") != std::string::npos)
                out.baseline_file = sv("baseline_file");
            else if (line.find("\"baseline_hash\"") != std::string::npos)
                out.baseline_hash = sv("baseline_hash");
            else if (line.find("\"rebase_threshold\"") != std::string::npos) {
                auto s = sv("rebase_threshold");
                if (!s.empty()) out.rebase_threshold = std::stof(s);
            }
            else if (line.find("\"anchor_interval\"") != std::string::npos) {
                auto s = sv("anchor_interval");
                if (!s.empty()) out.anchor_interval = std::stoi(s);
            }
        }
        else if (section == Section::VERSIONS) {
            if (line.find('{') != std::string::npos) {
                cur_ver = BackupVersion{};
                in_object = true;
                continue;
            }
            if (line.find('}') != std::string::npos && in_object) {
                out.versions.push_back(cur_ver);
                in_object = false;
                continue;
            }
            if (line.find(']') != std::string::npos) {
                section = Section::ROOT;
                continue;
            }
            if (in_object) {
                auto sv = [&](const std::string& k) { return json_read_string(line, k); };
                if (line.find("\"id\"") != std::string::npos)
                    cur_ver.id = sv("id");
                else if (line.find("\"label\"") != std::string::npos)
                    cur_ver.label = sv("label");
                else if (line.find("\"timestamp\"") != std::string::npos) {
                    auto s = sv("timestamp");
                    if (!s.empty()) cur_ver.timestamp = std::stoull(s);
                }
                else if (line.find("\"patch_file\"") != std::string::npos)
                    cur_ver.patch_file = sv("patch_file");
                else if (line.find("\"patch_size\"") != std::string::npos) {
                    auto s = sv("patch_size");
                    if (!s.empty()) cur_ver.patch_size = std::stoull(s);
                }
                else if (line.find("\"patch_hash\"") != std::string::npos)
                    cur_ver.patch_hash = sv("patch_hash");
                else if (line.find("\"relative_to_baseline\"") != std::string::npos)
                    cur_ver.relative_to_baseline = sv("relative_to_baseline");
            }
        }
        else if (section == Section::REBASE_HISTORY) {
            if (line.find('{') != std::string::npos) {
                cur_rec = RebaseRecord{};
                in_object = true;
                continue;
            }
            if (line.find('}') != std::string::npos && in_object) {
                out.rebase_history.push_back(cur_rec);
                in_object = false;
                continue;
            }
            if (line.find(']') != std::string::npos) {
                section = Section::ROOT;
                continue;
            }
            if (in_object) {
                auto sv = [&](const std::string& k) { return json_read_string(line, k); };
                if (line.find("\"from_baseline\"") != std::string::npos)
                    cur_rec.from_baseline = sv("from_baseline");
                else if (line.find("\"to_baseline\"") != std::string::npos)
                    cur_rec.to_baseline = sv("to_baseline");
                else if (line.find("\"timestamp\"") != std::string::npos) {
                    auto s = sv("timestamp");
                    if (!s.empty()) cur_rec.timestamp = std::stoull(s);
                }
                else if (line.find("\"versions_reexpressed\"") != std::string::npos) {
                    auto s = sv("versions_reexpressed");
                    if (!s.empty()) cur_rec.versions_reexpressed = std::stoi(s);
                }
            }
        }
    }
    return true;
}

// ============================================================
// Internal helpers
// ============================================================

static uint64_t now_unix() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

// Version id generation is handled by the command layer (cmds/backup.h).
// The backup_init / backup_snap functions receive an already-resolved id.

// Patch filename for a given version id
static std::string patch_filename(const std::string& version_id,
                                  const std::string& baseline_id) {
    return version_id + "_from_" + baseline_id + ".patch";
}

// Full path to patches directory inside store
static fs::path patches_dir(const std::string& store_path) {
    return fs::path(store_path) / PATCHES_SUBDIR;
}

// Find a version entry by id; returns nullptr if not found
static BackupVersion* find_version(BackupManifest& m, const std::string& id) {
    for (auto& v : m.versions)
        if (v.id == id) return &v;
    return nullptr;
}

static const BackupVersion* find_version(const BackupManifest& m, const std::string& id) {
    for (const auto& v : m.versions)
        if (v.id == id) return &v;
    return nullptr;
}

// Reconstruct a specific version's .cfup into dest_cfup path.
// If version is baseline, just copies baseline.cfup.
// Otherwise applies the stored patch against baseline.cfup.
static bool reconstruct_version(const std::string& store_path,
                                 const BackupManifest& manifest,
                                 const std::string& version_id,
                                 const std::string& dest_cfup) {
    const auto* ver = find_version(manifest, version_id);
    if (!ver) {
        Logger::Log(LOG_ERROR, "Version not found: " + version_id);
        return false;
    }

    fs::path baseline = fs::path(store_path) / manifest.baseline_file;

    if (ver->relative_to_baseline == "is_baseline") {
        // Just copy baseline
        std::error_code ec;
        fs::copy_file(baseline, dest_cfup,
                      fs::copy_options::overwrite_existing, ec);
        if (ec) {
            Logger::Log(LOG_ERROR, "Failed to copy baseline: " + ec.message());
            return false;
        }
        return true;
    }

    // Apply patch: baseline is always the source
    fs::path patch_path = patches_dir(store_path) / ver->patch_file;
    if (!fs::exists(patch_path)) {
        Logger::Log(LOG_ERROR, "Patch file missing: " + patch_path.string());
        return false;
    }

    // Verify patch integrity
    std::string actual_hash = fileHasher::xxhash_file(patch_path.string());
    if (actual_hash != ver->patch_hash) {
        Logger::Log(LOG_ERROR, "Patch hash mismatch for " + version_id +
                               " — file may be corrupted");
        return false;
    }

    Logger::Log(LOG_INFO, "Applying patch for " + version_id + "...");
    try {
        fastcdc::applyPatch(baseline.string().c_str(),
                            patch_path.string().c_str(),
                            dest_cfup.c_str());
    } catch (const std::exception& e) {
        Logger::Log(LOG_ERROR, "Patch application failed: " + std::string(e.what()));
        return false;
    }
    return true;
}

// ============================================================
// backup_init
// ============================================================

bool backup_init(const std::string& folder_path,
                 const std::string& store_path,
                 const std::string& version_id,
                 const std::string& label) {
    // Validate source
    if (!fs::exists(folder_path) || !fs::is_directory(folder_path)) {
        Logger::Log(LOG_ERROR, "Source is not a valid directory: " + folder_path);
        return false;
    }

    if (version_id.empty()) {
        Logger::Log(LOG_ERROR, "version_id must not be empty");
        return false;
    }

    // Create store directory structure
    std::error_code ec;
    fs::create_directories(fs::path(store_path) / PATCHES_SUBDIR, ec);
    if (ec) {
        Logger::Log(LOG_ERROR, "Failed to create store directory: " + ec.message());
        return false;
    }

    // Check store is fresh
    if (fs::exists(fs::path(store_path) / MANIFEST_FILENAME)) {
        Logger::Log(LOG_ERROR, "Store already initialised at: " + store_path +
                               " — use 'backup snap' to add snapshots");
        return false;
    }

    fs::path baseline_path = fs::path(store_path) / BASELINE_FILENAME;

    Logger::Log(LOG_INFO, "Packing folder to baseline snapshot...");
    Logger::StartTimer("baseline pack");

    if (!pack_folder(folder_path, baseline_path.string())) {
        Logger::Log(LOG_ERROR, "Failed to pack folder to baseline");
        return false;
    }

    Logger::EndTimer("baseline pack", LOG_INFO);

    std::string baseline_hash = fileHasher::xxhash_file(baseline_path.string());
    // version_id provided by caller — no auto-generation here

    BackupManifest manifest;
    manifest.baseline_id   = version_id;
    manifest.baseline_file = BASELINE_FILENAME;
    manifest.baseline_hash = baseline_hash;

    BackupVersion ver;
    ver.id                   = version_id;
    ver.label                = label.empty() ? "initial backup" : label;
    ver.timestamp            = now_unix();
    ver.patch_file           = "";
    ver.patch_size           = 0;
    ver.patch_hash           = "";
    ver.relative_to_baseline = "is_baseline";

    manifest.versions.push_back(ver);

    if (!write_manifest(store_path, manifest)) {
        Logger::Log(LOG_ERROR, "Failed to write manifest");
        return false;
    }

    Logger::Log(LOG_INFO, "Backup store initialised.");
    Logger::Log(LOG_INFO, "  Store:    " + store_path);
    Logger::Log(LOG_INFO, "  Baseline: " + version_id);
    Logger::Log(LOG_INFO, "  Size:     " + std::to_string(fs::file_size(baseline_path)) + " bytes");
    return true;
}

// ============================================================
// backup_snap
// ============================================================

bool backup_snap(const std::string& folder_path,
                 const std::string& store_path,
                 const std::string& version_id,
                 const std::string& label) {
    if (!fs::exists(folder_path) || !fs::is_directory(folder_path)) {
        Logger::Log(LOG_ERROR, "Source is not a valid directory: " + folder_path);
        return false;
    }

    if (version_id.empty()) {
        Logger::Log(LOG_ERROR, "version_id must not be empty");
        return false;
    }

    BackupManifest manifest;
    if (!read_manifest(store_path, manifest)) return false;

    fs::path baseline_path = fs::path(store_path) / manifest.baseline_file;
    if (!fs::exists(baseline_path)) {
        Logger::Log(LOG_ERROR, "Baseline missing from store: " + baseline_path.string());
        return false;
    }

    fs::path temp_cfup = fs::path(store_path) / (version_id + "_temp.cfup");

    Logger::Log(LOG_INFO, "Packing current folder state...");
    Logger::StartTimer("snap pack");

    if (!pack_folder(folder_path, temp_cfup.string())) {
        Logger::Log(LOG_ERROR, "Failed to pack folder for snapshot");
        return false;
    }

    Logger::EndTimer("snap pack", LOG_INFO);

    // Build patch filename
    std::string pfile = patch_filename(version_id, manifest.baseline_id);
    fs::path    patch_path = patches_dir(store_path) / pfile;

    Logger::Log(LOG_INFO, "Computing delta against baseline...");
    Logger::StartTimer("snap diff");

    try {
        fastcdc::createPatch(baseline_path.string().c_str(),
                             temp_cfup.string().c_str(),
                             patch_path.string().c_str());
    } catch (const std::exception& e) {
        Logger::Log(LOG_ERROR, "Diff failed: " + std::string(e.what()));
        fs::remove(temp_cfup);
        return false;
    }

    Logger::EndTimer("snap diff", LOG_INFO);

    // Discard temp cfup — we only keep the patch
    fs::remove(temp_cfup);

    uint64_t    psize = fs::file_size(patch_path);
    std::string phash = fileHasher::xxhash_file(patch_path.string());

    BackupVersion ver;
    ver.id                   = version_id;
    ver.label                = label;
    ver.timestamp            = now_unix();
    ver.patch_file           = pfile;
    ver.patch_size           = psize;
    ver.patch_hash           = phash;
    ver.relative_to_baseline = "after";

    manifest.versions.push_back(ver);

    if (!write_manifest(store_path, manifest)) {
        Logger::Log(LOG_ERROR, "Failed to write manifest");
        return false;
    }

    // Report rebase advisory
    RebaseMetrics metrics = backup_rebase_metrics(store_path);
    if (metrics.should_rebase) {
        Logger::Log(LOG_WARNING, "Advisory: " + metrics.reason);
        Logger::Log(LOG_WARNING, "  Suggested new baseline: " + metrics.suggested_baseline_id);
        Logger::Log(LOG_WARNING, "  Run: cfx backup rebase <store> --to " +
                                 metrics.suggested_baseline_id);
    }

    Logger::Log(LOG_INFO, "Snapshot created: " + version_id);
    Logger::Log(LOG_INFO, "  Patch size: " + std::to_string(psize) + " bytes");
    Logger::Log(LOG_INFO, "  Label:      " + label);
    return true;
}

// ============================================================
// backup_restore
// ============================================================

bool backup_restore(const std::string& store_path,
                    const std::string& version_id,
                    const std::string& dest_path) {
    BackupManifest manifest;
    if (!read_manifest(store_path, manifest)) return false;

    const auto* ver = find_version(manifest, version_id);
    if (!ver) {
        Logger::Log(LOG_ERROR, "Version not found in store: " + version_id);
        return false;
    }

    // If dest exists, warn but proceed
    if (fs::exists(dest_path)) {
        Logger::Log(LOG_WARNING, "Destination exists, contents may be overwritten: " + dest_path);
    }

    // If version IS baseline, skip patch step entirely
    if (ver->relative_to_baseline == "is_baseline") {
        fs::path baseline = fs::path(store_path) / manifest.baseline_file;
        Logger::Log(LOG_INFO, "Restoring baseline version " + version_id + "...");
        Logger::StartTimer("restore unpack");
        bool ok = unpack_packed_file(baseline.string(), dest_path);
        Logger::EndTimer("restore unpack", LOG_INFO);
        return ok;
    }

    // Reconstruct .cfup to temp
    fs::path temp_cfup = fs::path(store_path) /
                         (version_id + "_restore_temp.cfup");

    Logger::Log(LOG_INFO, "Reconstructing version " + version_id + " from baseline...");
    Logger::StartTimer("restore reconstruct");

    if (!reconstruct_version(store_path, manifest, version_id, temp_cfup.string())) {
        fs::remove(temp_cfup);
        return false;
    }

    Logger::EndTimer("restore reconstruct", LOG_INFO);
    Logger::Log(LOG_INFO, "Unpacking restored state...");
    Logger::StartTimer("restore unpack");

    bool ok = unpack_packed_file(temp_cfup.string(), dest_path);

    Logger::EndTimer("restore unpack", LOG_INFO);
    fs::remove(temp_cfup);

    if (ok) {
        Logger::Log(LOG_INFO, "Restore complete: " + dest_path);
    }
    return ok;
}

// ============================================================
// backup_rebase
// ============================================================

bool backup_rebase(const std::string& store_path,
                   const std::string& new_baseline_id) {
    BackupManifest manifest;
    if (!read_manifest(store_path, manifest)) return false;

    if (new_baseline_id == manifest.baseline_id) {
        Logger::Log(LOG_INFO, new_baseline_id + " is already the baseline. Nothing to do.");
        return true;
    }

    const auto* new_base_ver = find_version(manifest, new_baseline_id);
    if (!new_base_ver) {
        Logger::Log(LOG_ERROR, "Version not found: " + new_baseline_id);
        return false;
    }

    // Check available disk space (rough: need ~3x baseline size)
    uint64_t baseline_size = fs::file_size(fs::path(store_path) / manifest.baseline_file);
    auto space = fs::space(store_path);
    if (space.available < baseline_size * 3) {
        Logger::Log(LOG_ERROR, "Insufficient disk space for rebase. Need ~" +
                               std::to_string(baseline_size * 3 / 1024 / 1024) + " MB free.");
        return false;
    }

    // Staging directory — all new artifacts go here first
    fs::path staging = fs::path(store_path) / STAGING_SUBDIR;
    std::error_code ec;
    fs::remove_all(staging, ec);
    fs::create_directories(staging / PATCHES_SUBDIR, ec);
    if (ec) {
        Logger::Log(LOG_ERROR, "Failed to create staging directory: " + ec.message());
        return false;
    }

    // --- Step 1: Reconstruct new baseline .cfup ---
    Logger::Log(LOG_INFO, "Reconstructing new baseline: " + new_baseline_id);
    Logger::StartTimer("rebase new baseline");

    fs::path new_baseline_cfup = staging / BASELINE_FILENAME;

    if (!reconstruct_version(store_path, manifest, new_baseline_id,
                             new_baseline_cfup.string())) {
        fs::remove_all(staging);
        return false;
    }

    Logger::EndTimer("rebase new baseline", LOG_INFO);

    // --- Step 2: Re-express every other version against new baseline ---
    BackupManifest new_manifest    = manifest; // copy structure
    new_manifest.baseline_id       = new_baseline_id;
    new_manifest.baseline_file     = BASELINE_FILENAME;
    new_manifest.baseline_hash     = fileHasher::xxhash_file(new_baseline_cfup.string());
    new_manifest.versions.clear();

    int reexpressed = 0;

    // Determine ordering for "before"/"after" relative to new baseline
    // Find index of new baseline in version list
    int new_base_idx = -1;
    for (int i = 0; i < (int)manifest.versions.size(); ++i) {
        if (manifest.versions[i].id == new_baseline_id) {
            new_base_idx = i;
            break;
        }
    }

    for (int i = 0; i < (int)manifest.versions.size(); ++i) {
        const auto& ver = manifest.versions[i];

        if (ver.id == new_baseline_id) {
            // This becomes the new baseline entry
            BackupVersion bver = ver;
            bver.patch_file           = "";
            bver.patch_size           = 0;
            bver.patch_hash           = "";
            bver.relative_to_baseline = "is_baseline";
            new_manifest.versions.push_back(bver);
            continue;
        }

        Logger::Log(LOG_INFO, "Re-expressing " + ver.id + " against new baseline...");
        Logger::StartTimer("rebase " + ver.id);

        // Reconstruct this version's .cfup into a temp file
        fs::path temp_ver_cfup = staging / (ver.id + "_temp.cfup");

        if (!reconstruct_version(store_path, manifest, ver.id, temp_ver_cfup.string())) {
            Logger::Log(LOG_ERROR, "Failed to reconstruct " + ver.id + " — aborting rebase");
            fs::remove_all(staging);
            return false;
        }

        // Create new patch: new_baseline → this version
        std::string new_pfile = patch_filename(ver.id, new_baseline_id);
        fs::path    new_patch = staging / PATCHES_SUBDIR / new_pfile;

        try {
            fastcdc::createPatch(new_baseline_cfup.string().c_str(),
                                 temp_ver_cfup.string().c_str(),
                                 new_patch.string().c_str());
        } catch (const std::exception& e) {
            Logger::Log(LOG_ERROR, "Patch creation failed for " + ver.id +
                                   ": " + e.what());
            fs::remove_all(staging);
            return false;
        }

        Logger::EndTimer("rebase " + ver.id, LOG_INFO);

        // Discard temp version cfup
        fs::remove(temp_ver_cfup);

        uint64_t    psize = fs::file_size(new_patch);
        std::string phash = fileHasher::xxhash_file(new_patch.string());

        BackupVersion nver        = ver;
        nver.patch_file           = new_pfile;
        nver.patch_size           = psize;
        nver.patch_hash           = phash;
        nver.relative_to_baseline = (i < new_base_idx) ? "before" : "after";

        new_manifest.versions.push_back(nver);
        ++reexpressed;
    }

    // Record rebase history
    RebaseRecord rec;
    rec.from_baseline        = manifest.baseline_id;
    rec.to_baseline          = new_baseline_id;
    rec.timestamp            = now_unix();
    rec.versions_reexpressed = reexpressed;
    new_manifest.rebase_history = manifest.rebase_history;
    new_manifest.rebase_history.push_back(rec);

    // Write new manifest into staging
    if (!write_manifest(staging.string(), new_manifest)) {
        Logger::Log(LOG_ERROR, "Failed to write new manifest in staging");
        fs::remove_all(staging);
        return false;
    }

    // --- Step 3: Atomic swap — replace live store contents with staging ---
    Logger::Log(LOG_INFO, "Committing rebase...");

    // Remove old patches directory and baseline
    fs::remove_all(fs::path(store_path) / PATCHES_SUBDIR, ec);
    fs::remove(fs::path(store_path) / BASELINE_FILENAME, ec);
    fs::remove(fs::path(store_path) / MANIFEST_FILENAME, ec);

    // Move staging contents into store
    for (auto& entry : fs::directory_iterator(staging)) {
        fs::rename(entry.path(),
                   fs::path(store_path) / entry.path().filename(), ec);
        if (ec) {
            Logger::Log(LOG_ERROR, "Failed to commit staged file: " + ec.message());
            return false;
        }
    }

    fs::remove_all(staging, ec);

    Logger::Log(LOG_INFO, "Rebase complete.");
    Logger::Log(LOG_INFO, "  New baseline: " + new_baseline_id);
    Logger::Log(LOG_INFO, "  Versions re-expressed: " + std::to_string(reexpressed));
    return true;
}

// ============================================================
// backup_list
// ============================================================

bool backup_list(const std::string& store_path) {
    BackupManifest manifest;
    if (!read_manifest(store_path, manifest)) return false;

    Logger::Log(LOG_INFO, "Backup store: " + store_path);
    Logger::Log(LOG_INFO, "Baseline: " + manifest.baseline_id);
    Logger::Log(LOG_INFO, "");

    uint64_t baseline_size =
        fs::exists(fs::path(store_path) / manifest.baseline_file)
        ? fs::file_size(fs::path(store_path) / manifest.baseline_file)
        : 0;

    Logger::Log(LOG_INFO, std::string("  ") +
                std::string(60, '-'));
    Logger::Log(LOG_INFO, "  ID                       | Label                | Patch Size | Relation");
    Logger::Log(LOG_INFO, std::string("  ") +
                std::string(60, '-'));

    for (const auto& v : manifest.versions) {
        std::string id_col = v.id;
        while (id_col.size() < 24) id_col += ' ';

        std::string lbl_col = v.label.empty() ? "-" : v.label;
        while (lbl_col.size() < 20) lbl_col += ' ';

        std::string size_col;
        if (v.relative_to_baseline == "is_baseline") {
            size_col = std::to_string(baseline_size) + "B (base)";
        } else {
            size_col = std::to_string(v.patch_size) + "B";
        }
        while (size_col.size() < 10) size_col += ' ';

        std::string marker = (v.id == manifest.baseline_id) ? " [BASELINE]" : "";

        Logger::Log(LOG_INFO, "  " + id_col + " | " + lbl_col + " | " +
                              size_col + " | " + v.relative_to_baseline + marker);
    }

    Logger::Log(LOG_INFO, std::string("  ") +
                std::string(60, '-'));

    if (!manifest.rebase_history.empty()) {
        Logger::Log(LOG_INFO, "");
        Logger::Log(LOG_INFO, "Rebase history:");
        for (const auto& r : manifest.rebase_history) {
            Logger::Log(LOG_INFO, "  " + r.from_baseline + " → " +
                                  r.to_baseline + " (" +
                                  std::to_string(r.versions_reexpressed) +
                                  " versions re-expressed)");
        }
    }

    return true;
}

// ============================================================
// backup_drop
// ============================================================

bool backup_drop(const std::string& store_path,
                 const std::string& version_id) {
    BackupManifest manifest;
    if (!read_manifest(store_path, manifest)) return false;

    if (version_id == manifest.baseline_id) {
        Logger::Log(LOG_ERROR, "Cannot drop the baseline version. "
                               "Rebase to another version first, then drop.");
        return false;
    }

    auto it = std::find_if(manifest.versions.begin(), manifest.versions.end(),
                           [&](const BackupVersion& v){ return v.id == version_id; });

    if (it == manifest.versions.end()) {
        Logger::Log(LOG_ERROR, "Version not found: " + version_id);
        return false;
    }

    // Remove patch file
    if (!it->patch_file.empty()) {
        fs::path ppath = patches_dir(store_path) / it->patch_file;
        std::error_code ec;
        fs::remove(ppath, ec);
        if (ec) {
            Logger::Log(LOG_WARNING, "Could not remove patch file: " + ec.message());
        }
    }

    manifest.versions.erase(it);

    if (!write_manifest(store_path, manifest)) {
        Logger::Log(LOG_ERROR, "Failed to update manifest after drop");
        return false;
    }

    Logger::Log(LOG_INFO, "Dropped version: " + version_id);
    return true;
}

// ============================================================
// backup_prune
// ============================================================

bool backup_prune(const std::string& store_path, int keep_count) {
    if (keep_count < 1) {
        Logger::Log(LOG_ERROR, "keep_count must be >= 1");
        return false;
    }

    BackupManifest manifest;
    if (!read_manifest(store_path, manifest)) return false;

    // Separate baseline from the rest, sort rest by timestamp descending
    std::vector<BackupVersion*> non_base;
    for (auto& v : manifest.versions) {
        if (v.id != manifest.baseline_id)
            non_base.push_back(&v);
    }

    std::sort(non_base.begin(), non_base.end(),
              [](const BackupVersion* a, const BackupVersion* b){
                  return a->timestamp > b->timestamp; // newest first
              });

    if ((int)non_base.size() <= keep_count) {
        Logger::Log(LOG_INFO, "Nothing to prune (" +
                              std::to_string(non_base.size()) +
                              " non-baseline versions, keep=" +
                              std::to_string(keep_count) + ")");
        return true;
    }

    int dropped = 0;
    for (int i = keep_count; i < (int)non_base.size(); ++i) {
        Logger::Log(LOG_INFO, "Pruning: " + non_base[i]->id);
        if (!backup_drop(store_path, non_base[i]->id)) {
            Logger::Log(LOG_WARNING, "Failed to drop " + non_base[i]->id);
        } else {
            ++dropped;
        }
        // Re-read manifest after each drop since drop rewrites it
        if (!read_manifest(store_path, manifest)) return false;
    }

    Logger::Log(LOG_INFO, "Pruned " + std::to_string(dropped) + " versions.");

    // Check if auto-rebase is warranted after pruning
    RebaseMetrics metrics = backup_rebase_metrics(store_path);
    if (metrics.should_rebase) {
        Logger::Log(LOG_INFO, "Auto-rebase triggered: " + metrics.reason);
        return backup_rebase(store_path, metrics.suggested_baseline_id);
    }

    return true;
}

// ============================================================
// backup_rebase_metrics
// ============================================================

RebaseMetrics backup_rebase_metrics(const std::string& store_path) {
    RebaseMetrics result;
    result.should_rebase = false;

    BackupManifest manifest;
    if (!read_manifest(store_path, manifest)) return result;

    // Collect non-baseline versions in order
    std::vector<const BackupVersion*> ordered;
    for (const auto& v : manifest.versions) {
        if (v.relative_to_baseline != "is_baseline")
            ordered.push_back(&v);
    }

    if (ordered.size() < 3) return result; // Not enough data to evaluate

    // Sort by timestamp
    std::sort(ordered.begin(), ordered.end(),
              [](const BackupVersion* a, const BackupVersion* b){
                  return a->timestamp < b->timestamp;
              });

    // Check if recent patches are growing faster than threshold
    size_t n = ordered.size();
    size_t half = n / 2;

    uint64_t older_total = 0, recent_total = 0;
    for (size_t i = 0; i < half; ++i)          older_total  += ordered[i]->patch_size;
    for (size_t i = half; i < n; ++i)           recent_total += ordered[i]->patch_size;

    float older_avg  = older_total  / (float)half;
    float recent_avg = recent_total / (float)(n - half);

    float growth = (older_avg > 0) ? (recent_avg - older_avg) / older_avg : 0.0f;

    if (growth > manifest.rebase_threshold) {
        result.should_rebase = true;
        result.reason = "Patch sizes growing at " +
                        std::to_string((int)(growth * 100)) +
                        "% over baseline (threshold: " +
                        std::to_string((int)(manifest.rebase_threshold * 100)) + "%)";

        // Suggest midpoint version as new baseline (minimises max patch size)
        int mid_idx = static_cast<int>(ordered.size()) / 2;
        result.suggested_baseline_id = ordered[mid_idx]->id;
    }

    // Also trigger if version count exceeds anchor_interval
    if ((int)manifest.versions.size() > manifest.anchor_interval && !result.should_rebase) {
        result.should_rebase         = true;
        result.reason                = "Version count (" +
                                       std::to_string(manifest.versions.size()) +
                                       ") exceeds anchor interval (" +
                                       std::to_string(manifest.anchor_interval) + ")";
        int mid_idx = static_cast<int>(ordered.size()) / 2;
        result.suggested_baseline_id = ordered[mid_idx]->id;
    }

    return result;
}