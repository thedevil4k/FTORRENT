#ifndef SECURITYANALYZER_H
#define SECURITYANALYZER_H

#include "SecurityTypes.h"

#include <string>
#include <vector>

/**
 * @brief Metadata-only risk analysis for the files inside a torrent.
 *
 * This module never opens a file, never downloads a piece, and never talks to
 * a provider. It looks at the names and the metadata libtorrent already has:
 * extensions, hidden/executable flags, symlink targets and the path itself.
 * That is why it can run on the UI thread for a whole torrent in microseconds.
 *
 * The output is a warning, not a verdict. A clean report says "no known
 * indicators", never "safe": a name cannot prove what a file does.
 */
class SecurityAnalyzer {
public:
    // Analyzes every non-pad file. The report includes one Finding per file
    // plus torrent-level findings such as "many executables".
    static SecurityReport analyze(const std::vector<SecurityFile>& files);

    // Analyzes one file. Used by the per-file rows and by tests.
    static FileAssessment assessFile(const SecurityFile& file);

    // True when the file is meant to run code or open a shortcut/macro.
    // Archives and disk images are not included: they are containers, not
    // runnable files, and "select only non-executables" should leave them.
    static bool isRunnable(const SecurityFile& file);

    static bool isExecutablePath(const std::string& path);
    static bool isInstallerPath(const std::string& path);
    static bool isScriptPath(const std::string& path);
    static bool isShortcutPath(const std::string& path);
    static bool isMacroDocumentPath(const std::string& path);
    static bool isArchivePath(const std::string& path);
    static bool isDiskImagePath(const std::string& path);

    // Content-badge classifications. They are deliberately narrower than the
    // risk predicates above:
    //  - isDiscImagePath() only matches .iso/.dmg/.img, the formats a user
    //    recognises as a disc image; VM disks and WIM/ESD stay risk-only.
    //  - isVideoPath() leaves .ts out so a TypeScript file is not badged.
    //  - isNativeExecutablePath() matches binaries the OS runs directly and
    //    excludes .dll/.sys/.jar/.class and every script extension.
    static bool isDiscImagePath(const std::string& path);
    static bool isVideoPath(const std::string& path);
    static bool isNativeExecutablePath(const std::string& path);

    // True when a torrent's own title ends in an archive extension
    // ("...Optimized.zip", "show.tar.gz"). Only a hint: the title is written by
    // whoever published the torrent, so the search row draws this badge dimmed
    // until the real file list confirms it. The disc-image and native-binary
    // variants follow the same rule; video has no title hint because a normal
    // title does not end in .mkv and guessing from "1080p" would be a guess.
    static bool titleSuggestsArchive(const std::string& title);
    static bool titleSuggestsDiscImage(const std::string& title);
    static bool titleSuggestsNativeExecutable(const std::string& title);

    // Lowercased extension including the dot, empty when there is none.
    // Backslashes and forward slashes are both treated as separators.
    static std::string extensionLower(const std::string& path);

    // Short uppercase labels for the per-file rows, such as EXE, SCRIPT,
    // ARCHIVE, SYMLINK, HIDDEN or DOUBLE EXT.
    static std::vector<std::string> tagsForFile(const SecurityFile& file);

    static std::string findingTitle(FindingCode code);
    static std::string severityLabel(Severity severity);
    static std::string describeFinding(const Finding& finding);

private:
    SecurityAnalyzer() = delete;
};

#endif // SECURITYANALYZER_H
