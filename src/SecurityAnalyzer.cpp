#include "SecurityAnalyzer.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

std::string lowerAscii(std::string text) {
    for (char& ch : text) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return text;
}

std::string baseName(const std::string& path) {
    const std::size_t separator = path.find_last_of("/\\");
    return separator == std::string::npos ? path : path.substr(separator + 1);
}

bool hasSeparator(const std::string& path) {
    return path.find_first_of("/\\") != std::string::npos;
}

bool isAbsoluteLike(const std::string& path) {
    if (path.empty()) return false;
    if (path[0] == '/' || path[0] == '\\') return true;
    // A Windows drive letter, absolute or drive-relative (C:foo).
    return path.size() >= 2 &&
           std::isalpha(static_cast<unsigned char>(path[0])) &&
           path[1] == ':';
}

bool isUncLike(const std::string& path) {
    return path.size() >= 2 &&
           ((path[0] == '\\' && path[1] == '\\') ||
            (path[0] == '/' && path[1] == '/'));
}

bool hasTraversal(const std::string& path) {
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find_first_of("/\\", start);
        const std::size_t length =
            (end == std::string::npos ? path.size() : end) - start;
        if (length == 2 && path[start] == '.' && path[start + 1] == '.') {
            return true;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

bool hasTrailingDotOrSpace(const std::string& base) {
    if (base.empty()) return false;
    const char last = base.back();
    return last == '.' || last == ' ';
}

bool isReservedWindowsName(const std::string& base) {
    std::string stem = lowerAscii(base);
    const std::size_t dot = stem.find('.');
    if (dot != std::string::npos) stem = stem.substr(0, dot);
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul") {
        return true;
    }
    if (stem.size() == 4 && stem[3] >= '1' && stem[3] <= '9') {
        return (stem.compare(0, 3, "com") == 0) ||
               (stem.compare(0, 3, "lpt") == 0);
    }
    return false;
}

bool containsBidiControl(const std::string& text) {
    // UTF-8 encodings of the bidi controls that can visually reorder a name:
    // U+202A..U+202E, U+2066..U+2069, U+200E/U+200F and U+061C.
    static const unsigned char sequences[][3] = {
        {0xE2, 0x80, 0xAA}, {0xE2, 0x80, 0xAB}, {0xE2, 0x80, 0xAC},
        {0xE2, 0x80, 0xAD}, {0xE2, 0x80, 0xAE}, {0xE2, 0x80, 0x8E},
        {0xE2, 0x80, 0x8F}, {0xE2, 0x81, 0xA6}, {0xE2, 0x81, 0xA7},
        {0xE2, 0x81, 0xA8}, {0xE2, 0x81, 0xA9}
    };
    for (const auto& sequence : sequences) {
        if (text.find(reinterpret_cast<const char*>(sequence), 0, 3) !=
            std::string::npos) {
            return true;
        }
    }
    // U+061C ARABIC LETTER MARK.
    return text.find("\xD8\x9C", 2) != std::string::npos;
}

bool containsAsciiControl(const std::string& text) {
    for (unsigned char byte : text) {
        if (byte < 0x20 || byte == 0x7f) return true;
    }
    return false;
}

bool hasNonAscii(const std::string& text) {
    for (unsigned char byte : text) {
        if (byte >= 0x80) return true;
    }
    return false;
}

bool extensionIn(const std::string& extension,
                 std::initializer_list<const char*> list) {
    for (const char* item : list) {
        if (extension == item) return true;
    }
    return false;
}

bool hasDoubleExtension(const std::string& path) {
    const std::string extension = SecurityAnalyzer::extensionLower(path);
    if (!SecurityAnalyzer::isExecutablePath(path) &&
        !SecurityAnalyzer::isInstallerPath(path) &&
        !SecurityAnalyzer::isScriptPath(path) &&
        !SecurityAnalyzer::isShortcutPath(path) &&
        !SecurityAnalyzer::isMacroDocumentPath(path)) {
        return false;
    }

    const std::string base = baseName(path);
    const std::size_t lastDot = base.find_last_of('.');
    if (lastDot == std::string::npos || lastDot == 0) return false;
    const std::size_t previousDot = base.find_last_of('.', lastDot - 1);
    // A leading dot is a hidden-file marker, not a second extension.
    return previousDot != std::string::npos && previousDot > 0;
}

std::string previousExtension(const std::string& path) {
    const std::string base = baseName(path);
    const std::size_t lastDot = base.find_last_of('.');
    if (lastDot == std::string::npos || lastDot == 0) return {};
    const std::size_t previousDot = base.find_last_of('.', lastDot - 1);
    if (previousDot == std::string::npos) return {};
    return lowerAscii(base.substr(previousDot, lastDot - previousDot));
}

bool containsSuspiciousKeyword(const std::string& base) {
    static const char* const keywords[] = {
        "crack", "keygen", "patch", "activator", "serial",
        "nulled", "warez", "hacktool"
    };
    const std::string lower = lowerAscii(base);
    for (const char* keyword : keywords) {
        const std::string word(keyword);
        std::size_t pos = lower.find(word);
        while (pos != std::string::npos) {
            const bool leftOk = pos == 0 ||
                !std::isalnum(static_cast<unsigned char>(lower[pos - 1]));
            const std::size_t after = pos + word.size();
            const bool rightOk = after >= lower.size() ||
                !std::isalnum(static_cast<unsigned char>(lower[after]));
            if (leftOk && rightOk) return true;
            pos = lower.find(word, pos + 1);
        }
    }
    return false;
}

bool isLongPath(const std::string& path) {
    if (path.size() > 240) return true;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find_first_of("/\\", start);
        const std::size_t length =
            (end == std::string::npos ? path.size() : end) - start;
        if (length > 255) return true;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

RiskLevel riskForSeverity(Severity severity) {
    switch (severity) {
        case Severity::Low:      return RiskLevel::Low;
        case Severity::Medium:   return RiskLevel::Medium;
        case Severity::High:     return RiskLevel::High;
        case Severity::Critical: return RiskLevel::Critical;
        case Severity::Info:
        default:                 return RiskLevel::Unknown;
    }
}

bool isSuspiciousNameCode(FindingCode code) {
    switch (code) {
        case FindingCode::DoubleExtension:
        case FindingCode::RtlOverride:
        case FindingCode::ControlCharacter:
        case FindingCode::NonAsciiExtension:
        case FindingCode::PathTraversal:
        case FindingCode::AbsolutePath:
        case FindingCode::UncPath:
        case FindingCode::ReservedName:
        case FindingCode::TrailingDotOrSpace:
        case FindingCode::HiddenFile:
        case FindingCode::AutorunFile:
        case FindingCode::SystemMetadataFile:
        case FindingCode::SuspiciousKeyword:
        case FindingCode::LongPath:
            return true;
        default:
            return false;
    }
}

std::string joinParts(const std::vector<std::string>& parts) {
    std::ostringstream out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out << ", ";
        out << parts[i];
    }
    return out.str();
}

std::string countLabel(int count, const char* singular, const char* plural) {
    return std::to_string(count) + " " + (count == 1 ? singular : plural);
}

void addTag(std::vector<std::string>& tags, const char* tag) {
    for (const std::string& existing : tags) {
        if (existing == tag) return;
    }
    tags.emplace_back(tag);
}

// The end of a title, ignoring the padding publishers add after the file name
// (apibay pads with spaces) and a closing bracket that wraps it ("[repack.zip]").
// Returns the offset just past the last character that can belong to a name.
std::size_t titlePathEnd(const std::string& title) {
    std::size_t end = title.size();
    while (end > 0) {
        const char last = title[end - 1];
        if (std::isspace(static_cast<unsigned char>(last)) ||
            last == ']' || last == ')') {
            --end;
        } else {
            break;
        }
    }
    return end;
}

} // namespace

bool SecurityAnalyzer::isExecutablePath(const std::string& path) {
    const std::string extension = extensionLower(path);
    return extensionIn(extension, {
        ".exe", ".com", ".scr", ".pif", ".cpl", ".dll", ".sys",
        ".appimage", ".run", ".jar", ".class", ".elf", ".bin"
    });
}

bool SecurityAnalyzer::isInstallerPath(const std::string& path) {
    const std::string extension = extensionLower(path);
    return extensionIn(extension, {
        ".msi", ".msix", ".appx", ".msu", ".pkg", ".deb", ".rpm", ".apk"
    });
}

bool SecurityAnalyzer::isScriptPath(const std::string& path) {
    const std::string extension = extensionLower(path);
    return extensionIn(extension, {
        ".bat", ".cmd", ".ps1", ".psm1", ".vbs", ".vbe", ".js", ".jse",
        ".wsf", ".wsh", ".hta", ".reg", ".sh", ".bash", ".zsh", ".py",
        ".pyw", ".pl", ".rb", ".php", ".lua", ".scpt", ".applescript",
        ".command"
    });
}

bool SecurityAnalyzer::isShortcutPath(const std::string& path) {
    const std::string extension = extensionLower(path);
    return extensionIn(extension, {".lnk", ".url", ".desktop"});
}

bool SecurityAnalyzer::isMacroDocumentPath(const std::string& path) {
    const std::string extension = extensionLower(path);
    return extensionIn(extension, {
        ".docm", ".dotm", ".xlsm", ".xltm", ".xlam", ".pptm", ".potm",
        ".ppsm"
    });
}

bool SecurityAnalyzer::isArchivePath(const std::string& path) {
    const std::string extension = extensionLower(path);
    return extensionIn(extension, {
        ".zip", ".rar", ".7z", ".tar", ".gz", ".bz2", ".xz", ".zst",
        ".cab", ".arj", ".lzh", ".lha"
    });
}

bool SecurityAnalyzer::isDiskImagePath(const std::string& path) {
    const std::string extension = extensionLower(path);
    return extensionIn(extension, {
        ".iso", ".img", ".vhd", ".vhdx", ".vmdk", ".dmg", ".wim", ".esd"
    });
}

bool SecurityAnalyzer::isDiscImagePath(const std::string& path) {
    const std::string extension = extensionLower(path);
    return extensionIn(extension, {".iso", ".dmg", ".img"});
}

bool SecurityAnalyzer::isVideoPath(const std::string& path) {
    const std::string extension = extensionLower(path);
    return extensionIn(extension, {
        ".mp4", ".m4v", ".mkv", ".avi", ".mov", ".wmv", ".flv", ".webm",
        ".mpg", ".mpeg", ".m2ts", ".mts", ".vob", ".ogv", ".3gp", ".3g2",
        ".divx", ".rm", ".rmvb", ".asf", ".f4v", ".mxf"
    });
}

bool SecurityAnalyzer::isNativeExecutablePath(const std::string& path) {
    const std::string extension = extensionLower(path);
    return extensionIn(extension, {
        ".exe", ".com", ".scr", ".pif", ".cpl",
        ".appimage", ".run", ".elf"
    });
}

bool SecurityAnalyzer::titleSuggestsArchive(const std::string& title) {
    // Only the end of the title is looked at: an archive named in the middle
    // says little about what the torrent actually contains.
    const std::size_t end = titlePathEnd(title);
    return end > 0 && isArchivePath(title.substr(0, end));
}

bool SecurityAnalyzer::titleSuggestsDiscImage(const std::string& title) {
    const std::size_t end = titlePathEnd(title);
    return end > 0 && isDiscImagePath(title.substr(0, end));
}

bool SecurityAnalyzer::titleSuggestsNativeExecutable(const std::string& title) {
    const std::size_t end = titlePathEnd(title);
    return end > 0 && isNativeExecutablePath(title.substr(0, end));
}

std::string SecurityAnalyzer::extensionLower(const std::string& path) {
    const std::size_t separator = path.find_last_of("/\\");
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos ||
        (separator != std::string::npos && dot < separator)) {
        return {};
    }

    std::string extension = path.substr(dot);
    for (char& ch : extension) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return extension;
}

bool SecurityAnalyzer::isRunnable(const SecurityFile& file) {
    if (file.padFile) return false;
    if (file.executable) return true;
    return isExecutablePath(file.path) || isInstallerPath(file.path) ||
           isScriptPath(file.path) || isShortcutPath(file.path) ||
           isMacroDocumentPath(file.path);
}

FileAssessment SecurityAnalyzer::assessFile(const SecurityFile& file) {
    FileAssessment assessment;
    if (file.padFile || file.path.empty()) return assessment;

    const std::string base = baseName(file.path);
    const std::string extension = extensionLower(file.path);
    const bool script = isScriptPath(file.path);
    const bool shortcut = isShortcutPath(file.path);
    const bool macro = isMacroDocumentPath(file.path);
    const bool installer = isInstallerPath(file.path);
    const bool executable = isExecutablePath(file.path) || file.executable;
    const bool archive = isArchivePath(file.path);
    const bool disk = isDiskImagePath(file.path);
    const bool runnable = script || shortcut || macro || installer ||
                          executable;
    assessment.runnable = runnable;

    std::vector<Finding>& findings = assessment.findings;
    const auto add = [&](FindingCode code, Severity severity,
                         std::string detail) {
        Finding finding;
        finding.code = code;
        finding.severity = severity;
        finding.fileIndex = file.fileIndex;
        finding.detail = std::move(detail);
        findings.push_back(std::move(finding));
    };

    if (script) {
        add(FindingCode::Script, Severity::High, base);
    } else if (shortcut) {
        add(FindingCode::Shortcut, Severity::High, base);
    } else if (macro) {
        add(FindingCode::MacroDocument, Severity::High, base);
    } else if (installer) {
        add(FindingCode::Installer, Severity::Medium, base);
    } else if (executable) {
        add(FindingCode::Executable, Severity::Medium, base);
    }
    if (archive) {
        add(FindingCode::Archive, Severity::Medium, base);
    }
    if (disk) {
        add(FindingCode::DiskImage, Severity::Medium, base);
    }

    if (hasTraversal(file.path)) {
        add(FindingCode::PathTraversal, Severity::Critical, file.path);
    }
    if (file.absolutePath || isAbsoluteLike(file.path)) {
        add(isUncLike(file.path) ? FindingCode::UncPath
                                 : FindingCode::AbsolutePath,
            Severity::Critical, file.path);
    }
    if (isReservedWindowsName(base)) {
        add(FindingCode::ReservedName, Severity::High, base);
    }
    if (hasTrailingDotOrSpace(base)) {
        add(FindingCode::TrailingDotOrSpace, Severity::Medium, base);
    }
    if (isLongPath(file.path)) {
        add(FindingCode::LongPath, Severity::Medium, file.path);
    }

    if (hasDoubleExtension(file.path)) {
        add(FindingCode::DoubleExtension, Severity::Critical,
            base + " (looks like " + previousExtension(file.path) +
            ", runs as " + extension + ")");
    }
    if (containsBidiControl(file.path)) {
        add(FindingCode::RtlOverride, Severity::Critical, base);
    }
    if (containsAsciiControl(file.path)) {
        add(FindingCode::ControlCharacter, Severity::High, base);
    }
    if (hasNonAscii(extension)) {
        add(FindingCode::NonAsciiExtension, Severity::High, base);
    }

    const bool hidden = file.hidden ||
        (!base.empty() && base[0] == '.');
    if (hidden) {
        add(FindingCode::HiddenFile, Severity::Low, base);
    }
    if (lowerAscii(base) == "autorun.inf") {
        add(FindingCode::AutorunFile, Severity::High, base);
    }
    if (lowerAscii(base) == "desktop.ini" ||
        lowerAscii(base) == "thumbs.db") {
        add(FindingCode::SystemMetadataFile, Severity::Low, base);
    }
    if (containsSuspiciousKeyword(base)) {
        add(FindingCode::SuspiciousKeyword, Severity::Low, base);
    }

    if (file.symlink || !file.symlinkPath.empty()) {
        std::string detail = base + " -> " +
            (file.symlinkPath.empty() ? "unknown target" : file.symlinkPath);
        if (isAbsoluteLike(file.symlinkPath) ||
            hasTraversal(file.symlinkPath)) {
            detail = base + " -> " + file.symlinkPath +
                     " (target is outside the torrent)";
        }
        add(FindingCode::Symlink, Severity::Critical, detail);
    }

    if (runnable && file.size == 0) {
        add(FindingCode::ZeroByteExecutable, Severity::High, base);
    } else if (runnable && file.size > 0 && file.size < 512) {
        add(FindingCode::SizeTypeMismatch, Severity::Medium, base);
    }
    if (runnable && !hasSeparator(file.path)) {
        add(FindingCode::RootExecutable, Severity::Medium, base);
    }

    // Hidden and "crack/keygen" names are only context on their own. Next to
    // something that can run, they are a meaningful warning.
    if (runnable) {
        for (Finding& finding : findings) {
            if (finding.code == FindingCode::HiddenFile ||
                finding.code == FindingCode::SuspiciousKeyword) {
                finding.severity = Severity::High;
            }
        }
    }

    for (const Finding& finding : findings) {
        const RiskLevel risk = riskForSeverity(finding.severity);
        if (risk > assessment.level) assessment.level = risk;
    }
    if (assessment.level == RiskLevel::Unknown) assessment.level = RiskLevel::Low;

    std::vector<std::string>& tags = assessment.tags;
    if (script) addTag(tags, "SCRIPT");
    else if (shortcut) addTag(tags, "LNK");
    else if (macro) addTag(tags, "MACRO");
    else if (installer) addTag(tags, "INSTALLER");
    else if (executable) addTag(tags, "EXE");
    if (archive) addTag(tags, "ARCHIVE");
    if (disk) addTag(tags, "DISK");

    for (const Finding& finding : findings) {
        switch (finding.code) {
            case FindingCode::DoubleExtension: addTag(tags, "DOUBLE EXT"); break;
            case FindingCode::RtlOverride:     addTag(tags, "RTL"); break;
            case FindingCode::ControlCharacter:addTag(tags, "CONTROL"); break;
            case FindingCode::NonAsciiExtension:addTag(tags, "NON-ASCII"); break;
            case FindingCode::PathTraversal:
            case FindingCode::AbsolutePath:
            case FindingCode::UncPath:
            case FindingCode::ReservedName:
            case FindingCode::TrailingDotOrSpace:
            case FindingCode::LongPath:        addTag(tags, "PATH"); break;
            case FindingCode::HiddenFile:      addTag(tags, "HIDDEN"); break;
            case FindingCode::AutorunFile:     addTag(tags, "AUTORUN"); break;
            case FindingCode::SystemMetadataFile: addTag(tags, "SYSTEM"); break;
            case FindingCode::SuspiciousKeyword: addTag(tags, "SUSPICIOUS"); break;
            case FindingCode::Symlink:         addTag(tags, "SYMLINK"); break;
            case FindingCode::RootExecutable:  addTag(tags, "ROOT"); break;
            case FindingCode::ZeroByteExecutable:
            case FindingCode::SizeTypeMismatch:addTag(tags, "SIZE"); break;
            default: break;
        }
    }

    return assessment;
}

SecurityReport SecurityAnalyzer::analyze(const std::vector<SecurityFile>& files) {
    SecurityReport report;
    if (files.empty()) {
        report.summary = "No file list available. Metadata risk is unknown.";
        return report;
    }

    for (const SecurityFile& file : files) {
        if (file.padFile) continue;
        ++report.files;

        const FileAssessment assessment = assessFile(file);
        for (const Finding& finding : assessment.findings) {
            report.findings.push_back(finding);
        }
        if (assessment.level > report.level) report.level = assessment.level;

        if (isExecutablePath(file.path) || isInstallerPath(file.path) ||
            file.executable) {
            ++report.executables;
        }
        if (isScriptPath(file.path)) ++report.scripts;
        if (isArchivePath(file.path)) ++report.archives;
        if (isDiscImagePath(file.path)) ++report.discImages;
        if (isVideoPath(file.path)) ++report.videos;
        if (isNativeExecutablePath(file.path)) ++report.nativeExecutables;
        if (isShortcutPath(file.path)) ++report.shortcuts;
        if (file.symlink || !file.symlinkPath.empty()) ++report.symlinks;

        bool suspiciousName = false;
        for (const Finding& finding : assessment.findings) {
            if (isSuspiciousNameCode(finding.code)) {
                suspiciousName = true;
                break;
            }
        }
        if (suspiciousName) ++report.suspiciousNameCount;
    }

    if (report.files == 0) {
        report.level = RiskLevel::Unknown;
        report.summary = "No file list available. Metadata risk is unknown.";
        return report;
    }

    if (report.executables >= 3) {
        Finding finding;
        finding.code = FindingCode::ManyExecutables;
        finding.severity = report.executables >= 6 ? Severity::High
                                                   : Severity::Medium;
        finding.fileIndex = -1;
        finding.detail = countLabel(report.executables, "executable file",
                                    "executable files");
        report.findings.push_back(finding);
        const RiskLevel risk = riskForSeverity(finding.severity);
        if (risk > report.level) report.level = risk;
    }

    if (report.findings.empty()) {
        report.level = RiskLevel::Low;
        report.summary = "No known indicators in file names or metadata.";
        return report;
    }

    std::vector<std::string> parts;
    if (report.executables > 0) {
        parts.push_back(countLabel(report.executables, "executable",
                                   "executables"));
    }
    if (report.scripts > 0) {
        parts.push_back(countLabel(report.scripts, "script", "scripts"));
    }
    if (report.archives > 0) {
        parts.push_back(countLabel(report.archives, "archive", "archives"));
    }
    if (report.shortcuts > 0) {
        parts.push_back(countLabel(report.shortcuts, "shortcut", "shortcuts"));
    }
    if (report.symlinks > 0) {
        parts.push_back(countLabel(report.symlinks, "symlink", "symlinks"));
    }
    if (report.suspiciousNameCount > 0) {
        parts.push_back(countLabel(report.suspiciousNameCount,
                                   "suspicious name", "suspicious names"));
    }
    if (parts.empty()) {
        parts.push_back(countLabel(static_cast<int>(report.findings.size()),
                                   "metadata finding", "metadata findings"));
    }

    std::string summary = joinParts(parts) + ".";
    if (report.level == RiskLevel::Critical ||
        report.level == RiskLevel::High) {
        summary += " Review before downloading.";
    } else if (report.level == RiskLevel::Medium) {
        summary += " Review before running.";
    } else {
        summary += " Low-risk indicators only.";
    }
    report.summary = std::move(summary);
    return report;
}

std::vector<std::string> SecurityAnalyzer::tagsForFile(
    const SecurityFile& file) {
    return assessFile(file).tags;
}

std::string SecurityAnalyzer::findingTitle(FindingCode code) {
    switch (code) {
        case FindingCode::Executable:         return "Executable file";
        case FindingCode::Installer:          return "Installer";
        case FindingCode::Script:             return "Script";
        case FindingCode::Shortcut:           return "Shortcut";
        case FindingCode::MacroDocument:      return "Macro-enabled document";
        case FindingCode::Archive:            return "Archive";
        case FindingCode::DiskImage:          return "Disk image";
        case FindingCode::DoubleExtension:    return "Double extension";
        case FindingCode::RtlOverride:        return "Right-to-left override";
        case FindingCode::ControlCharacter:   return "Control character";
        case FindingCode::NonAsciiExtension:  return "Non-ASCII extension";
        case FindingCode::PathTraversal:      return "Path traversal";
        case FindingCode::AbsolutePath:       return "Absolute path";
        case FindingCode::UncPath:            return "UNC path";
        case FindingCode::ReservedName:       return "Windows reserved name";
        case FindingCode::TrailingDotOrSpace: return "Trailing dot or space";
        case FindingCode::HiddenFile:         return "Hidden file";
        case FindingCode::AutorunFile:        return "Autorun file";
        case FindingCode::SystemMetadataFile: return "System metadata file";
        case FindingCode::SuspiciousKeyword:  return "Suspicious name";
        case FindingCode::Symlink:            return "Symlink";
        case FindingCode::RootExecutable:     return "Executable at torrent root";
        case FindingCode::ManyExecutables:    return "Many executables";
        case FindingCode::ZeroByteExecutable: return "Zero-byte executable";
        case FindingCode::SizeTypeMismatch:   return "Suspiciously small executable";
        case FindingCode::LongPath:           return "Long path";
        case FindingCode::Unknown:
        default:                              return "Metadata finding";
    }
}

std::string SecurityAnalyzer::severityLabel(Severity severity) {
    switch (severity) {
        case Severity::Info:     return "Info";
        case Severity::Low:      return "Low";
        case Severity::Medium:   return "Medium";
        case Severity::High:     return "High";
        case Severity::Critical: return "Critical";
        default:                 return "Info";
    }
}

std::string SecurityAnalyzer::describeFinding(const Finding& finding) {
    const std::string title = findingTitle(finding.code);
    if (finding.detail.empty()) return title;
    return title + ": " + finding.detail;
}
