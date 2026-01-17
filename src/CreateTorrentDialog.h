#ifndef CREATETORRENTDIALOG_H
#define CREATETORRENTDIALOG_H

#include <FL/Fl_Window.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Multiline_Input.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Progress.H>
#include <string>
#include <vector>

class CreateTorrentDialog : public Fl_Window {
public:
    CreateTorrentDialog();
    ~CreateTorrentDialog();

    bool show_modal();

private:
    // UI components
    Fl_Input* m_sourcePath;
    Fl_Button* m_btnSelectFile;
    Fl_Button* m_btnSelectFolder;
    
    Fl_Choice* m_pieceSize;
    Fl_Multiline_Input* m_trackers;
    Fl_Button* m_btnAddGenericTrackers;
    Fl_Multiline_Input* m_webSeeds;
    Fl_Multiline_Input* m_comment;
    Fl_Input* m_origin;
    
    Fl_Check_Button* m_isPrivate;
    Fl_Check_Button* m_startSeeding;
    Fl_Check_Button* m_optimizeAlignment;
    
    Fl_Progress* m_progress;
    Fl_Button* m_btnCreate;
    Fl_Button* m_btnCancel;

    bool m_okClicked;

    // UI creation
    void createUI();
    void createButtons();
    
    // Callbacks
    static void onSelectFile(Fl_Widget* w, void* data);
    static void onSelectFolder(Fl_Widget* w, void* data);
    static void onCreate(Fl_Widget* w, void* data);
    static void onCancel(Fl_Widget* w, void* data);
    static void onAddGenericTrackers(Fl_Widget* w, void* data);

    void processCreate();
};

#endif // CREATETORRENTDIALOG_H
