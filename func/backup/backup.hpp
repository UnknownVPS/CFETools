#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>

// =============================================
// Version entry in the manifest
// =============================================
struct BackupVersion {
    std::string id;                     // e.g. "v1", "v2", or timestamp string
    std::string label;                  // user-provided label, optional
    uint64_t    timestamp;              // unix timestamp of when snapshot was taken
    std::string patch_file;             // relative path inside store, empty if baseline
    uint64_t    patch_size;             // bytes, 0 if baseline
    std::string patch_hash;             // xxhash3 hex of patch file, empty if baseline
    std::string relative_to_baseline;  // "before", "after", "is_baseline"
};

// =============================================
// Rebase history entry
// =============================================
struct RebaseRecord {
    std::string from_baseline;
    std::string to_baseline;
    uint64_t    timestamp;
    int         versions_reexpressed;
};

// =============================================
// Full manifest
// =============================================
struct BackupManifest {
    int         store_version    = 1;
    std::string baseline_id;
    std::string baseline_file;          // always "baseline.cfup"
    std::string baseline_hash;          // xxhash3 hex of baseline.cfup
    float       rebase_threshold = 0.3f;
    int         anchor_interval  = 10;

    std::vector<BackupVersion> versions;
    std::vector<RebaseRecord>  rebase_history;
};

// =============================================
// Rebase metrics / suggestion
// =============================================
struct RebaseMetrics {
    bool        should_rebase;
    std::string suggested_baseline_id;  // version id closest to midpoint
    std::string reason;                 // human-readable explanation
};

// =============================================
// Public API
// =============================================

/**
 * Initialize a new backup store for the given folder.
 * Creates store directory, packs folder as baseline.cfup, writes manifest.
 *
 * @param folder_path   Folder to back up
 * @param store_path    Directory that will become the backup store
 * @param label         Optional label for the initial snapshot
 * @return true on success
 */
bool backup_init(const std::string& folder_path,
                 const std::string& store_path,
                 const std::string& version_id,
                 const std::string& label = "initial backup");

/**
 * Create a new incremental snapshot.
 * Packs folder → temp .cfup → diffs against baseline → stores patch.
 *
 * @param folder_path   Folder to snapshot
 * @param store_path    Existing backup store directory
 * @param version_id    Explicit version identifier (e.g. "v2")
 * @param label         Optional label for this snapshot
 * @return true on success
 */
bool backup_snap(const std::string& folder_path,
                 const std::string& store_path,
                 const std::string& version_id,
                 const std::string& label = "");

/**
 * Restore a specific version to a destination folder.
 * Applies patch against baseline (or directly unpacks baseline if version IS baseline).
 *
 * @param store_path    Backup store directory
 * @param version_id    Version to restore (e.g. "v3")
 * @param dest_path     Destination folder (created if not exists)
 * @return true on success
 */
bool backup_restore(const std::string& store_path,
                    const std::string& version_id,
                    const std::string& dest_path);

/**
 * Rebase the store to a new baseline version.
 * Reconstructs all other versions as patches against the new baseline.
 * Old patches are replaced atomically via staging directory.
 *
 * @param store_path        Backup store directory
 * @param new_baseline_id   Version to promote to baseline
 * @return true on success
 */
bool backup_rebase(const std::string& store_path,
                   const std::string& new_baseline_id);

/**
 * List all versions in the store with metadata.
 *
 * @param store_path    Backup store directory
 * @return true on success
 */
bool backup_list(const std::string& store_path);

/**
 * Drop a specific version's patch from the store.
 * Cannot drop the baseline version; rebase first.
 *
 * @param store_path    Backup store directory
 * @param version_id    Version to drop
 * @return true on success
 */
bool backup_drop(const std::string& store_path,
                 const std::string& version_id);

/**
 * Prune old versions, keeping the N most recent (plus baseline).
 * Triggers auto-rebase if rebase metrics indicate it's warranted.
 *
 * @param store_path    Backup store directory
 * @param keep_count    Number of most-recent versions to retain
 * @return true on success
 */
bool backup_prune(const std::string& store_path,
                  int keep_count);

/**
 * Analyse current store and return rebase recommendation.
 *
 * @param store_path    Backup store directory
 * @return RebaseMetrics struct
 */
RebaseMetrics backup_rebase_metrics(const std::string& store_path);

/**
 * Read manifest from store. Returns false if store is invalid.
 */
bool read_manifest(const std::string& store_path, BackupManifest& out);

/**
 * Write manifest to store.
 */
bool write_manifest(const std::string& store_path, const BackupManifest& manifest);