#ifndef SYSTEM_UTILS_H
#define SYSTEM_UTILS_H

#include <string>
#include <vector>

/**
 * @brief Cross-platform system utilities for FTorrent
 */
class SystemUtils {
public:
    /**
     * @brief Get the current process RAM usage as a formatted string
     */
    static std::string getRamUsage();

    /**
     * @brief Get the standard configuration directory for the current OS
     */
    static std::string getConfigDir();

    /**
     * @brief Get the default downloads directory for the current OS
     */
    static std::string getDefaultDownloadsDir();

    /**
     * @brief Check if the current OS is Windows
     */
    static bool isWindows();

    /**
     * @brief Check if the current OS is Linux
     */
    static bool isLinux();

    /**
     * @brief Check if the current OS is macOS
     */
    static bool isMacOS();

    /**
     * @brief Open a folder in the system file explorer
     */
    static void openFolder(const std::string& path);

    /**
     * @brief Set the process priority (CPU/IO)
     * @param high True for high priority, false for normal
     */
    static void setProcessPriority(bool high);

    /**
     * @brief Get system architecture (x86, x64, arm, etc.)
     */
    static std::string getArchitecture();

    /**
     * @brief Forces the OS to release unused memory and trim the working set
     */
    static void releaseMemory();
};

#endif // SYSTEM_UTILS_H
