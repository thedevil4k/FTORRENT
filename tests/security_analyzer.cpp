// SecurityAnalyzer checks.
//
// The analyzer is the whole of phase 1's detection: it reads names and the
// metadata libtorrent already carries, never a byte of content. These checks
// pin down the classification of every file type, the hostile-name findings,
// the torrent-level summary and -- importantly -- the wording. A clean report
// must say "no known indicators", never "safe".
#include "SecurityAnalyzer.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

int checks = 0;
int failures = 0;

void ok(bool condition, const char* what) {
    checks++;
    if (!condition) {
        failures++;
        std::cout << "    FAIL " << what << '\n';
    }
}

void section(const char* title) {
    std::cout << "\n== " << title << " ==\n";
}

SecurityFile file(const std::string& path, std::int64_t size = 1024) {
    SecurityFile f;
    f.path = path;
    f.size = size;
    return f;
}

bool hasCode(const FileAssessment& assessment, FindingCode code) {
    for (const Finding& finding : assessment.findings) {
        if (finding.code == code) return true;
    }
    return false;
}

bool hasTag(const FileAssessment& assessment, const std::string& tag) {
    for (const std::string& existing : assessment.tags) {
        if (existing == tag) return true;
    }
    return false;
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

} // namespace

int main() {
    section("extension parsing");
    ok(SecurityAnalyzer::extensionLower("SETUP.EXE") == ".exe",
       "extensions are lowercased");
    ok(SecurityAnalyzer::extensionLower("dir.v2/setup.ExE") == ".exe",
       "the last extension after a separator wins");
    ok(SecurityAnalyzer::extensionLower("C:\\temp\\a.tar.gz") == ".gz",
       "backslashes are separators too");
    ok(SecurityAnalyzer::extensionLower("no-extension") == "",
       "a name without a dot has no extension");
    ok(SecurityAnalyzer::extensionLower("dir.with.dot/readme") == "",
       "a dot in a folder is not an extension");

    section("file families");
    ok(SecurityAnalyzer::isExecutablePath("a.EXE"), "exe is executable");
    ok(SecurityAnalyzer::isExecutablePath("a.jar"), "jar is executable");
    ok(SecurityAnalyzer::isInstallerPath("a.msi"), "msi is an installer");
    ok(SecurityAnalyzer::isInstallerPath("a.apk"), "apk is an installer");
    ok(SecurityAnalyzer::isScriptPath("a.PS1"), "ps1 is a script");
    ok(SecurityAnalyzer::isShortcutPath("a.lnk"), "lnk is a shortcut");
    ok(SecurityAnalyzer::isMacroDocumentPath("a.docm"), "docm is a macro document");
    ok(SecurityAnalyzer::isArchivePath("a.RAR"), "rar is an archive");
    ok(SecurityAnalyzer::isDiskImagePath("a.iso"), "iso is a disk image");
    ok(!SecurityAnalyzer::isExecutablePath("a.txt"), "txt is not executable");

    section("runnable files");
    ok(SecurityAnalyzer::isRunnable(file("setup.exe")), "exe runs");
    ok(SecurityAnalyzer::isRunnable(file("install.bat")), "script runs");
    ok(SecurityAnalyzer::isRunnable(file("game.msi")), "installer runs");
    ok(!SecurityAnalyzer::isRunnable(file("album.zip")),
       "an archive is a container, not code");
    ok(!SecurityAnalyzer::isRunnable(file("image.iso")),
       "a disk image is a container, not code");
    SecurityFile flagged = file("payload.dat");
    flagged.executable = true;
    ok(SecurityAnalyzer::isRunnable(flagged),
       "the executable metadata flag is honoured without an extension");
    SecurityFile pad = file("setup.exe");
    pad.padFile = true;
    ok(!SecurityAnalyzer::isRunnable(pad), "a pad file never runs");

    section("ordinary files");
    {
        const FileAssessment a = SecurityAnalyzer::assessFile(file("photo.jpg"));
        ok(a.level == RiskLevel::Low, "a plain picture is low risk");
        ok(a.findings.empty(), "a plain picture has no findings");
        ok(a.tags.empty(), "a plain picture has no tags");
        ok(!a.runnable, "a plain picture does not run");
    }
    {
        const FileAssessment a = SecurityAnalyzer::assessFile(file("setup.exe"));
        ok(a.level == RiskLevel::Medium, "an exe alone is medium risk");
        ok(hasCode(a, FindingCode::Executable), "the exe is reported");
        ok(hasCode(a, FindingCode::RootExecutable),
           "an exe at the torrent root is called out");
        ok(hasTag(a, "EXE"), "the exe gets an EXE tag");
        ok(a.runnable, "the exe is runnable");
    }
    {
        const FileAssessment a = SecurityAnalyzer::assessFile(file("install.bat"));
        ok(a.level == RiskLevel::High, "a script is high risk");
        ok(hasCode(a, FindingCode::Script), "the script is reported");
        ok(hasTag(a, "SCRIPT"), "the script gets a SCRIPT tag");
    }
    {
        const FileAssessment a = SecurityAnalyzer::assessFile(file("game.msi"));
        ok(a.level == RiskLevel::Medium, "an installer alone is medium risk");
        ok(hasCode(a, FindingCode::Installer), "the installer is reported");
    }
    {
        const FileAssessment a = SecurityAnalyzer::assessFile(file("album.zip"));
        ok(a.level == RiskLevel::Medium, "an archive is medium risk");
        ok(hasCode(a, FindingCode::Archive), "the archive is reported");
        ok(hasTag(a, "ARCHIVE"), "the archive gets an ARCHIVE tag");
    }
    {
        const FileAssessment a = SecurityAnalyzer::assessFile(file("movie.iso"));
        ok(a.level == RiskLevel::Medium, "a disk image is medium risk");
        ok(hasCode(a, FindingCode::DiskImage), "the disk image is reported");
    }

    section("hostile names");
    {
        const FileAssessment a =
            SecurityAnalyzer::assessFile(file("invoice.pdf.exe"));
        ok(a.level == RiskLevel::Critical, "a double extension is critical");
        ok(hasCode(a, FindingCode::DoubleExtension),
           "the double extension is reported");
        ok(hasTag(a, "DOUBLE EXT"), "the double extension gets a tag");
    }
    {
        const FileAssessment a =
            SecurityAnalyzer::assessFile(file("..\\..\\evil.exe"));
        ok(a.level == RiskLevel::Critical, "path traversal is critical");
        ok(hasCode(a, FindingCode::PathTraversal), "traversal is reported");
    }
    {
        const FileAssessment a =
            SecurityAnalyzer::assessFile(file("C:\\Windows\\evil.exe"));
        ok(a.level == RiskLevel::Critical, "an absolute path is critical");
        ok(hasCode(a, FindingCode::AbsolutePath), "the absolute path is reported");
    }
    {
        const FileAssessment a =
            SecurityAnalyzer::assessFile(file("\\\\server\\share\\evil.exe"));
        ok(hasCode(a, FindingCode::UncPath), "a UNC path is reported as UNC");
        ok(!hasCode(a, FindingCode::AbsolutePath),
           "a UNC path is not double-counted as absolute");
    }
    {
        const FileAssessment a =
            SecurityAnalyzer::assessFile(file("evil.\xE2\x80\xAE" "exe.txt"));
        ok(a.level == RiskLevel::Critical, "a bidi override is critical");
        ok(hasCode(a, FindingCode::RtlOverride), "the override is reported");
    }
    {
        const FileAssessment a = SecurityAnalyzer::assessFile(
            file("evil.\xEF\xBD\x85\xEF\xBD\x98\xEF\xBD\x85"));
        ok(a.level == RiskLevel::High, "a non-ASCII extension is high risk");
        ok(hasCode(a, FindingCode::NonAsciiExtension),
           "the non-ASCII extension is reported");
    }
    {
        const FileAssessment a = SecurityAnalyzer::assessFile(file("con.txt"));
        ok(a.level == RiskLevel::High, "a reserved device name is high risk");
        ok(hasCode(a, FindingCode::ReservedName), "the reserved name is reported");
    }
    {
        const FileAssessment a = SecurityAnalyzer::assessFile(file("evil.exe."));
        ok(hasCode(a, FindingCode::TrailingDotOrSpace),
           "a trailing dot is reported");
        ok(a.level == RiskLevel::Medium, "a trailing dot alone is medium risk");
    }
    {
        const FileAssessment a = SecurityAnalyzer::assessFile(file(".hidden.exe"));
        ok(a.level == RiskLevel::High,
           "hidden plus runnable is high risk, not low");
        ok(hasTag(a, "HIDDEN"), "the hidden marker gets a tag");
    }
    {
        const FileAssessment a =
            SecurityAnalyzer::assessFile(file("autorun.inf"));
        ok(a.level == RiskLevel::High, "autorun.inf is high risk");
        ok(hasCode(a, FindingCode::AutorunFile), "autorun.inf is reported");
    }
    {
        const FileAssessment a =
            SecurityAnalyzer::assessFile(file("keygen.exe"));
        ok(a.level == RiskLevel::High,
           "a crack keyword next to an executable is high risk");
        ok(hasTag(a, "SUSPICIOUS"), "the keyword gets a tag");
    }
    {
        SecurityFile link = file("docs/passwords");
        link.symlink = true;
        link.symlinkPath = "../../etc/passwd";
        const FileAssessment a = SecurityAnalyzer::assessFile(link);
        ok(a.level == RiskLevel::Critical, "a symlink is critical");
        ok(hasCode(a, FindingCode::Symlink), "the symlink is reported");
        ok(contains(SecurityAnalyzer::describeFinding(a.findings.front()),
                    "outside the torrent"),
           "an escaping symlink says its target is outside the torrent");
    }
    {
        const FileAssessment a = SecurityAnalyzer::assessFile(file("tool.exe", 0));
        ok(a.level == RiskLevel::High, "a zero-byte executable is high risk");
        ok(hasCode(a, FindingCode::ZeroByteExecutable),
           "the zero-byte executable is reported");
    }

    section("torrent report");
    {
        const SecurityReport r = SecurityAnalyzer::analyze({});
        ok(r.level == RiskLevel::Unknown, "an empty list is unknown");
        ok(!r.hasFileList(), "an empty list says there is no file list");
        ok(contains(r.summary, "unknown"), "the empty summary says unknown");
    }
    {
        std::vector<SecurityFile> files = {
            file("readme.txt"), file("photo.jpg"), file("music.mp3")
        };
        const SecurityReport r = SecurityAnalyzer::analyze(files);
        ok(r.files == 3, "all three files are counted");
        ok(r.level == RiskLevel::Low, "a clean list is low risk");
        ok(r.findings.empty(), "a clean list has no findings");
        ok(r.summary == "No known indicators in file names or metadata.",
           "a clean list uses the exact non-verdict wording");
        ok(!contains(r.summary, "safe"), "a clean list never says safe");
    }
    {
        std::vector<SecurityFile> files = {
            file("setup.exe"), file("crack.exe"), file("tool.exe")
        };
        const SecurityReport r = SecurityAnalyzer::analyze(files);
        ok(r.executables == 3, "three executables are counted");
        bool many = false;
        for (const Finding& f : r.findings) {
            if (f.code == FindingCode::ManyExecutables) many = true;
        }
        ok(many, "three executables add the torrent-level finding");
        ok(r.level >= RiskLevel::Medium, "three executables are at least medium");
        ok(contains(r.summary, "3 executables"), "the summary counts them");
    }
    {
        std::vector<SecurityFile> files;
        for (int i = 0; i < 6; ++i) {
            files.push_back(file("bin/tool" + std::to_string(i) + ".exe"));
        }
        const SecurityReport r = SecurityAnalyzer::analyze(files);
        ok(r.executables == 6, "six executables are counted");
        ok(r.level == RiskLevel::High,
           "six executables are high risk, not medium");
    }
    {
        std::vector<SecurityFile> files = {
            file("install.bat"), file("album.zip"), file("notes.txt")
        };
        const SecurityReport r = SecurityAnalyzer::analyze(files);
        ok(r.scripts == 1, "scripts are counted");
        ok(r.archives == 1, "archives are counted");
        ok(r.level == RiskLevel::High, "a script raises the torrent to high");
        ok(contains(r.summary, "1 script"), "the summary mentions the script");
        ok(contains(r.summary, "1 archive"), "the summary mentions the archive");
        ok(contains(r.summary, "Review before downloading"),
           "a high-risk list asks for review before downloading");
    }
    {
        SecurityFile pad = file("__pad.exe");
        pad.padFile = true;
        std::vector<SecurityFile> files = {file("readme.txt"), pad};
        const SecurityReport r = SecurityAnalyzer::analyze(files);
        ok(r.files == 1, "pad files are not counted as content");
        ok(r.executables == 0, "pad files are not counted as executables");
    }

    section("text");
    ok(SecurityAnalyzer::findingTitle(FindingCode::DoubleExtension) ==
           "Double extension",
       "finding titles are stable");
    ok(SecurityAnalyzer::severityLabel(Severity::Critical) == "Critical",
       "severity labels are stable");
    {
        Finding f;
        f.code = FindingCode::Executable;
        f.detail = "setup.exe";
        ok(SecurityAnalyzer::describeFinding(f) == "Executable file: setup.exe",
           "a finding description combines title and detail");
    }

    std::cout << "\nsecurity_analyzer checks=" << checks
              << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
