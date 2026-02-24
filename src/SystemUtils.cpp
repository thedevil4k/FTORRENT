#include "SystemUtils.h"
#include <sstream>
#include <iomanip>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <wininet.h>
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "wininet.lib")
#elif __APPLE__
#include <mach/mach.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <pwd.h>
#include <fstream>
#endif

std::string SystemUtils::getRamUsage() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        // En Windows, WorkingSetSize incluye el caché de archivos mapeados por el SO.
        // PrivateUsage representa lo que el programa tiene asignado de forma real y privada (Commit Size).
        double memMB = pmc.PrivateUsage / (1024.0 * 1024.0);
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << memMB << " MB";
        return oss.str();
    }
#elif __APPLE__
    struct mach_task_basic_info info;
    mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &infoCount) == KERN_SUCCESS) {
        double memMB = info.resident_size / (1024.0 * 1024.0);
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << memMB << " MB";
        return oss.str();
    }
#else
    // Linux /proc/self/status
    std::ifstream file("/proc/self/status");
    std::string line;
    while (std::getline(file, line)) {
        if (line.compare(0, 6, "VmRSS:") == 0) {
            std::string val = line.substr(7);
            // val is in kB
            long kb = std::stol(val);
            double memMB = kb / 1024.0;
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1) << memMB << " MB";
            return oss.str();
        }
    }
#endif
    return "0.0 MB";
}

void SystemUtils::releaseMemory() {
#ifdef _WIN32
    // Vaciar el "Working Set" del proceso. Esto obliga a Windows a liberar
    // toda la RAM posible de los archivos mapeados que no se estén usando activamente.
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
#endif
}

std::string SystemUtils::getConfigDir() {
    std::string path;
#ifdef _WIN32
    char buffer[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, buffer))) {
        path = std::string(buffer) + "\\FTorrent";
        CreateDirectoryA(path.c_str(), NULL);
    } else {
        path = ".";
    }
#elif __APPLE__
    const char* home = getenv("HOME");
    if (home) {
        path = std::string(home) + "/Library/Application Support/FTorrent";
    } else {
        path = ".";
    }
#else
    // Linux XDG_CONFIG_HOME
    const char* configHome = getenv("XDG_CONFIG_HOME");
    if (configHome) {
        path = std::string(configHome) + "/ftorrent";
    } else {
        const char* home = getenv("HOME");
        if (home) {
            path = std::string(home) + "/.config/ftorrent";
        } else {
            path = ".";
        }
    }
#endif
    return path;
}

std::string SystemUtils::getDefaultDownloadsDir() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, buffer))) {
        return std::string(buffer) + "\\Downloads";
    }
    return "C:\\Downloads";
#else
    const char* home = getenv("HOME");
    if (home) {
        return std::string(home) + "/Downloads";
    }
    return "/tmp";
#endif
}

bool SystemUtils::isWindows() {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

bool SystemUtils::isLinux() {
#ifdef __linux__
    return true;
#else
    return false;
#endif
}

bool SystemUtils::isMacOS() {
#ifdef __APPLE__
    return true;
#else
    return false;
#endif
}

void SystemUtils::openFolder(const std::string& path) {
#ifdef _WIN32
    ShellExecuteA(NULL, "open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
#elif __APPLE__
    std::string cmd = "open \"" + path + "\"";
    system(cmd.c_str());
#else
    std::string cmd = "xdg-open \"" + path + "\"";
    system(cmd.c_str());
#endif
}

void SystemUtils::setProcessPriority(bool high) {
#ifdef _WIN32
    SetPriorityClass(GetCurrentProcess(), high ? HIGH_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS);
#else
    // Nice value for Linux/Mac
    nice(high ? -10 : 0);
#endif
}

std::string SystemUtils::getArchitecture() {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) return "x64";
    if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) return "x86";
    if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) return "arm64";
    return "unknown";
#else
    // Linux/Mac architecture is usually determined at compile time or via uname
#if defined(__x86_64__) || defined(_M_X64)
    return "x64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#else
    return "unknown";
#endif
#endif
}

void SystemUtils::fetchPublicIpAndCountry(std::function<void(std::string, std::string)> callback) {
    if (!callback) return;
    
    std::thread([callback]() {
        std::string ip = "";
        std::string country = "";
        std::string url = "http://ip-api.com/line/?fields=status,countryCode,query";

#ifdef _WIN32
        HINTERNET hInternet = InternetOpenA("FTorrent", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (hInternet) {
            HINTERNET hConnect = InternetOpenUrlA(hInternet, url.c_str(), NULL, 0, INTERNET_FLAG_RELOAD, 0);
            if (hConnect) {
                char buffer[1024];
                DWORD bytesRead = 0;
                std::string response = "";
                while (InternetReadFile(hConnect, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
                    buffer[bytesRead] = '\0';
                    response += buffer;
                }
                InternetCloseHandle(hConnect);
                
                // Parse response: line 1: status, line 2: countryCode, line 3: ip
                std::stringstream ss(response);
                std::string status;
                std::getline(ss, status);
                if (!status.empty() && status.back() == '\r') status.pop_back();
                if (status == "success") {
                    std::getline(ss, country);
                    if (!country.empty() && country.back() == '\r') country.pop_back();
                    std::getline(ss, ip);
                    if (!ip.empty() && ip.back() == '\r') ip.pop_back();
                }
            }
            InternetCloseHandle(hInternet);
        }
#else
        // Use popen with curl on Linux/macOS
        std::string cmd = "curl -s \"" + url + "\"";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (pipe) {
            char buffer[1024];
            std::string response = "";
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                response += buffer;
            }
            pclose(pipe);
            
            std::stringstream ss(response);
            std::string status;
            std::getline(ss, status);
            if (!status.empty() && status.back() == '\r') status.pop_back();
            if (status == "success") {
                std::getline(ss, country);
                if (!country.empty() && country.back() == '\r') country.pop_back();
                std::getline(ss, ip);
                if (!ip.empty() && ip.back() == '\r') ip.pop_back();
            }
        }
#endif
        
        callback(ip, country);
    }).detach();
}
