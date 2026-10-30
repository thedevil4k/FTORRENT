#ifndef SECURITYTYPES_H
#define SECURITYTYPES_H

#include <cstdint>
#include <string>
#include <vector>

enum class RiskLevel {
    Unknown = 0,
    Low,
    Medium,
    High,
    Critical
};

enum class Severity {
    Info = 0,
    Low,
    Medium,
    High,
    Critical
};

/**
 * @brief One file as the security analyzer sees it.
 *
 * The fields are deliberately plain data: no libtorrent types and no FLTK.
 * TorrentFileList fills them from a .torrent or from magnet metadata, and
 * TorrentItem fills them for an active torrent. That keeps the analyzer pure
 * and easy to test.
 */
struct SecurityFile {
    int fileIndex = -1;       // libtorrent index, -1 when unknown
    std::string path;         // Path inside the torrent, as stored
    std::int64_t size = 0;    // Bytes
    bool padFile = false;     // Internal piece-alignment file
    bool hidden = false;      // Hidden attribute or a leading dot
    bool executable = false;  // Executable attribute in the torrent metadata
    bool symlink = false;
    bool absolutePath = false;
    std::string symlinkPath;  // Target when symlink is true
};

enum class FindingCode {
    Unknown = 0,
    Executable,
    Installer,
    Script,
    Shortcut,
    MacroDocument,
    Archive,
    DiskImage,
    DoubleExtension,
    RtlOverride,
    ControlCharacter,
    NonAsciiExtension,
    PathTraversal,
    AbsolutePath,
    UncPath,
    ReservedName,
    TrailingDotOrSpace,
    HiddenFile,
    AutorunFile,
    SystemMetadataFile,
    SuspiciousKeyword,
    Symlink,
    RootExecutable,
    ManyExecutables,
    ZeroByteExecutable,
    SizeTypeMismatch,
    LongPath
};

struct Finding {
    FindingCode code = FindingCode::Unknown;
    Severity severity = Severity::Info;
    int fileIndex = -1;       // -1 means a torrent-level finding
    std::string detail;
};

struct FileAssessment {
    RiskLevel level = RiskLevel::Unknown;
    std::vector<Finding> findings;
    std::vector<std::string> tags;
    bool runnable = false;
};

struct SecurityReport {
    RiskLevel level = RiskLevel::Unknown;
    std::vector<Finding> findings;
    int files = 0;
    int executables = 0;
    int scripts = 0;
    int archives = 0;
    int shortcuts = 0;
    int symlinks = 0;
    int suspiciousNameCount = 0;
    std::string summary;

    bool hasFileList() const { return files > 0; }
};

#endif // SECURITYTYPES_H
