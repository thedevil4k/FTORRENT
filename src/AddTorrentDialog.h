#ifndef ADDTORRENTDIALOG_H
#define ADDTORRENTDIALOG_H

#include <FL/Fl_Window.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Check_Browser.H>
#include <string>
#include <vector>

/**
 * @brief Diálogo avanzado para añadir torrents
 * 
 * Permite:
 * - Seleccionar archivo .torrent o ingresar magnet link
 * - Elegir ruta de descarga
 * - Opción de iniciar inmediatamente o pausado
 * - Seleccionar prioridad
 */
class AddTorrentDialog : public Fl_Window {
public:
    enum class Priority {
        Normal = 0,
        High = 1,
        Low = 2
    };
    
    AddTorrentDialog();
    ~AddTorrentDialog();
    
    // Show dialog and return true if OK was clicked
    bool show_modal();
    
    // Get results
    std::string getTorrentPath() const { return m_torrentPath; }
    std::string getMagnetLink() const { return m_magnetLink; }
    std::string getSavePath() const { return m_savePath; }
    bool getStartImmediately() const { return m_startImmediatelyResult; }
    Priority getPriority() const { return m_priority; }
    std::vector<int> getFilePriorities() const { return m_filePriorities; }
    bool isMagnet() const { return !m_magnetLink.empty(); }
    
    // Setters for pre-filling
    void setTorrentPath(const std::string& path);
    void setMagnetLink(const std::string& magnet);
    void setSavePath(const std::string& path);
    
private:
    
    // Common widgets
    Fl_Input* m_savePathInput;
    Fl_Button* m_browseSavePath;
    Fl_Check_Button* m_startImmediately;
    Fl_Choice* m_priorityChoice;
    
    // File-specific widgets
    Fl_Input* m_filePathInput;
    Fl_Button* m_browseFile;
    
    // Magnet-specific widgets
    Fl_Input* m_magnetInput;
    
    // Files display
    Fl_Check_Browser* m_fileBrowser;
    
    // Buttons
    Fl_Button* m_okButton;
    Fl_Button* m_cancelButton;
    
    // Results
    std::string m_torrentPath;
    std::string m_magnetLink;
    std::string m_savePath;
    std::vector<int> m_filePriorities;
    bool m_startImmediatelyResult;
    Priority m_priority;
    bool m_okClicked;
    
    // UI Creation
    void createUI();
    void createButtons();
    
    // Validation
    bool validate();
    
    // Callbacks
    static void onBrowseFile(Fl_Widget* w, void* data);
    static void onBrowseSavePath(Fl_Widget* w, void* data);
    static void onOK(Fl_Widget* w, void* data);
    static void onCancel(Fl_Widget* w, void* data);
    
    // Helper
    void loadDefaults();
    void updateFileList(const std::string& torrentPath);
};

#endif // ADDTORRENTDIALOG_H
