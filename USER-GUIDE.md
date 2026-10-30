# 📖 User Guide - FTorrent

Welcome to FTorrent! This guide will help you take your first steps with the lightest and most powerful torrent client.

## 🚀 Quick Start

1.  **Run the application:** Open the `FTorrent.exe` executable.
2.  **Initialize Session:** When you open it for the first time, you will see the main interface. The download engine starts automatically.
3.  **Configure Downloads:** Go to `Preferences` (gear icon) and choose your default download folder.

## 📥 How to Add Torrents

You have two ways to start downloading:

### 1. .torrent Files
Click the **"+"** icon or the `Add Torrent` button. Select the file from your computer and accept.

### 2. Magnet Links
Click the `Add Torrent` button, paste the magnet link in the corresponding field, and accept.

## ⚙️ Network Settings

To get the best performance, you can adjust:

-   **Download Limit:** Useful if you want to browse while downloading.
-   **Upload Limit:** Recommended to leave some room to not saturate your connection.
-   **Ports:** FTorrent uses UPnP to open ports automatically, but you can configure it manually in the Preferences.

## 💎 System Tray Integration

FTorrent can run in the background:
-   **Minimize to Tray:** Closing the window or clicking minimize (if configured) will move FTorrent to the system tray.
-   **Context Menu:** Right-click the tray icon to quickly Pause/Resume all torrents or Exit the application.
-   **Quick Restore:** Double-click the tray icon to bring the window back to front.

## 🔒 Privacy & Stealth

FTorrent includes built-in features to protect your sensitive information:
-   **IP Censorship:** Click the **eye icon** in the status bar to mask your public IP address. This is useful for streamers or when sharing screenshots.
-   **Persistent Privacy:** Your censorship preference is saved automatically for future sessions.

## 📊 Understanding the Interface

-   **Torrent List:** Shows the name, size, progress, speed, and status of each download.
-   **Status Bar:** At the bottom, you will see:
    -   **Total Speed:** Combined download and upload rates.
    -   **Privacy Toggle:** Hidden/Visible eye icon to mask your public IP and sensitive data.
    -   **Resource Monitor:** Real-time RAM consumption of the application.
    -   **Session Status:** Indicates if the engine is running or paused.
-   **Control Icons:**
    -   ▶️ **Resume:** Starts the selected download.
    -   ⏸️ **Pause:** Temporarily stops the download.
    -   🗑️ **Remove:** Removes the torrent (you can choose to also delete the downloaded files).

## 🛡️ Security Analysis

FTorrent analyzes a torrent **before** the download starts. The shield next to the name tells you what the metadata shows:

-   **Unknown** (grey): there is no file list yet. A magnet that was never expanded has no names to inspect.
-   **Low risk**: no known indicators in the names or metadata.
-   **Medium risk**: for example an executable, installer, archive or disk image.
-   **High risk**: a script, shortcut, macro-enabled document, hidden runnable file or other strong warning.
-   **Critical risk**: a double extension, path traversal, absolute/UNC path, bidi override or symlink.

The shield appears in search results, the Add Torrent dialog, the torrent list and the Details window. Expanding a search result shows a summary line and one tagged line per file (`EXE`, `SCRIPT`, `ARCHIVE`, `DOUBLE EXT`, `PATH`, `SYMLINK`, `HIDDEN`, and so on). Details has a **Security** tab with the complete report.

In **Add Torrent**, the panel analyzes the files that are currently checked. **Select only non-executables** unchecks executables, scripts, shortcuts and macro documents, but keeps archives and disk images because they are containers. If the checked selection is High or Critical, FTorrent asks for confirmation before accepting it. When a magnet has no resolved file list, expand its search row first; without metadata the panel stays **Unknown**.

The analysis reads names and the metadata libtorrent already carries: size, hidden/executable/symlink flags and symlink targets. **No file contents are read, no file is uploaded and no telemetry is sent.** Reputation lookups against external providers are a later phase and are not contacted by this build.

A clean report means **no known indicators**, not that the file is safe: a name cannot prove what a program does. Treat the shield as a warning system, not a guarantee. The former post-download **Open in VirusTotal** and **Inspect Archive** actions remain retired.

## ❓ Frequently Asked Questions

**Where are my files saved?**
By default in your system's download folder, but you can change it in the Preferences.

**Why is my download not progressing?**
Make sure the torrent has "Seeds". Without seeds, there is no one sharing the complete file.

---
*FTorrent - Simple, Fast, Mythical.*
