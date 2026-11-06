#include "MainWindow.h"
#include <FL/Fl.H>
#include <FL/x.H>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#ifdef _WIN32
void MainWindow::setupTrayIcon() {
    if (m_trayHasIcon) return;

    HWND hwnd = fl_xid(this);
    if (!hwnd) return;

    // Try to get the window icon set by FLTK icon() call
    HICON hIcon = (HICON)SendMessage(hwnd, WM_GETICON, ICON_BIG, 0);
    if (!hIcon) hIcon = (HICON)SendMessage(hwnd, WM_GETICON, ICON_SMALL, 0);
    if (!hIcon) hIcon = (HICON)GetClassLongPtr(hwnd, GCLP_HICON);
    if (!hIcon) hIcon = (HICON)GetClassLongPtr(hwnd, GCLP_HICONSM);

    // Fallback to application icon
    if (!hIcon) {
        hIcon = LoadIcon(GetModuleHandle(NULL), IDI_APPLICATION);
    }
    if (!hIcon) {
        hIcon = LoadIcon(NULL, IDI_APPLICATION);
    }

    NOTIFYICONDATA nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY_MESSAGE;
    nid.hIcon = hIcon;

    // Set tooltip
    strncpy(nid.szTip, "FTorrent", sizeof(nid.szTip) - 1);
    nid.szTip[sizeof(nid.szTip) - 1] = '\0';

    if (Shell_NotifyIcon(NIM_ADD, &nid)) {
        m_trayHasIcon = true;
    }
}

void MainWindow::removeTrayIcon() {
    if (!m_trayHasIcon) return;

    HWND hwnd = fl_xid(this);
    if (!hwnd) return;

    NOTIFYICONDATA nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;

    Shell_NotifyIcon(NIM_DELETE, &nid);
    m_trayHasIcon = false;
}

int MainWindow::win_event_handler(int) {
    extern MSG fl_msg;

    // ── WM_DROPFILES: user dropped files from Windows Explorer ────────────
    if (fl_msg.message == WM_DROPFILES) {
        MainWindow* win = (MainWindow*)Fl::first_window();
        if (win && win->m_manager) {
            HDROP hDrop = (HDROP)fl_msg.wParam;
            UINT count = DragQueryFileA(hDrop, 0xFFFFFFFF, NULL, 0);
            for (UINT i = 0; i < count; i++) {
                char buf[MAX_PATH] = {};
                DragQueryFileA(hDrop, i, buf, MAX_PATH);
                std::string path(buf);
                // Only process .torrent files
                if (path.size() > 8 &&
                    path.substr(path.size() - 8) == ".torrent") {
                    win->showAddTorrentDialog(path);
                }
            }
            DragFinish(hDrop);
        }
        return 1;
    }
    // ─────────────────────────────────────────────────────────────────────

    if (fl_msg.message == WM_TRAY_MESSAGE) {
        if (LOWORD(fl_msg.lParam) == WM_LBUTTONDBLCLK ||
            LOWORD(fl_msg.lParam) == WM_LBUTTONUP) {
            // Show/Restore window
            MainWindow* win = (MainWindow*)Fl::first_window();
            if (win) {
                if (win->visible()) {
                    win->hide();
                } else {
                    win->show();
                    // Bring to front
                    SetForegroundWindow(fl_xid(win));
                }
            }
            return 1;
        } else if (LOWORD(fl_msg.lParam) == WM_RBUTTONUP) {
            // Show a simple context menu
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, 1, "Open FTorrent");
            AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hMenu, MF_STRING, 3, "Pause All");
            AppendMenu(hMenu, MF_STRING, 4, "Resume All");
            AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hMenu, MF_STRING, 2, "Exit");

            POINT pt;
            GetCursorPos(&pt);

            // SetForegroundWindow is necessary for the menu to close when
            // clicking away
            SetForegroundWindow(fl_msg.hwnd);

            int id = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x,
                                    pt.y, 0, fl_msg.hwnd, NULL);
            DestroyMenu(hMenu);

            MainWindow* win = (MainWindow*)Fl::first_window();
            if (!win) return 0;

            if (id == 1) {
                win->show();
                SetForegroundWindow(fl_xid(win));
            } else if (id == 2) {
                // Actual exit from program
                win->removeTrayIcon();
                // Close the session and exit
                exit(0);
            } else if (id == 3) {
                // Pause all (Optional: could be implemented in manager)
                if (win->m_manager) {
                    for (auto* it : win->m_manager->getAllTorrents()) {
                        win->m_manager->pauseTorrent(it->getHash());
                    }
                }
            } else if (id == 4) {
                // Resume all
                if (win->m_manager) {
                    for (auto* it : win->m_manager->getAllTorrents()) {
                        win->m_manager->resumeTorrent(it->getHash());
                    }
                }
            }
            return 1;
        }
    }
    return 0;
}
#endif
