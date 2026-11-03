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
    // True when the container's own index says this entry needs a password to
    // be read (a RAR/ZIP file header carrying the encryption flag). Only the
    // level-2 archive check can know this: a torrent's file list lists names,
    // not archive headers, so it stays false everywhere else.
    bool encrypted = false;
    // Whole-file SHA-256, lowercase hex, when the metadata itself carries it.
    // A BitTorrent v2 pieces root is the SHA-256 of the file only when the file
    // is a single 16 KiB block; for anything larger it is a Merkle root and
    // must never be sent to a reputation provider as a file hash.
    std::string sha256;
    // True when the hash above is the whole file (not a partial or derived
    // value). Only then may a provider be asked about it.
    bool sha256Complete = false;
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
    // Content badges for the search rows. These are narrower than the risk
    // counters above on purpose: 'discImages' only covers the formats a user
    // calls a disc image, 'videos' is purely informational, and
    // 'nativeExecutables' leaves out scripts and bytecode even though the
    // risk analysis still flags those. 'encrypted' counts entries whose own
    // archive header says they are password-protected, which only the level-2
    // walk of an archive index can supply.
    int discImages = 0;
    int videos = 0;
    int nativeExecutables = 0;
    int encrypted = 0;
    // Whole-file SHA-256 the torrent metadata itself carries, and the file it
    // belongs to. Empty when the metadata carries none: FTorrent never
    // downloads a file just to compute a hash. The user is entitled to see
    // which hash was used, whether or not a provider ever answers.
    std::string sha256;
    std::string sha256Path;
    int suspiciousNameCount = 0;
    std::string summary;

    bool hasFileList() const { return files > 0; }
};

/**
 * @brief Where a reputation lookup stands.
 *
 * NoCompleteHash is the normal answer before a download and is not an error:
 * the torrent metadata simply does not carry the whole-file SHA-256, and
 * FTorrent refuses to download the file just to compute one.
 */
enum class ReputationState {
    NotRequested = 0,
    NoCompleteHash,
    Querying,
    Known,
    Unknown,
    NoKey,
    Error
};

struct ProviderVerdict {
    std::string provider;     // "VirusTotal", "MalwareBazaar"
    bool answered = false;    // The provider gave a usable answer
    bool known = false;       // It has a record for this hash
    bool needsKey = false;    // Not asked: no API key is configured
    int malicious = 0;
    int suspicious = 0;
    int total = 0;            // Engines reporting, 0 when the provider has none
    std::string summary;      // One line, safe to show as-is
};

struct ReputationResult {
    ReputationState state = ReputationState::NotRequested;
    std::string sha256;       // Lowercase hex, empty when unavailable
    std::vector<ProviderVerdict> verdicts;
    std::string message;      // Ready to display; never says "safe"
};

// The exact wording the product requires when there is no whole-file hash
// before the download. Kept here so every surface shows the same sentence.
inline const char* reputationNoHashMessage() {
    return "VT no disponible antes de descargar el archivo completo.";
}

#endif // SECURITYTYPES_H
