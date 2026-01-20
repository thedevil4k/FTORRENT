#ifndef REMOVECONFIRMDIALOG_H
#define REMOVECONFIRMDIALOG_H

#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Box.H>
#include <string>

class RemoveConfirmDialog : public Fl_Double_Window {
public:
    RemoveConfirmDialog(int count);
    ~RemoveConfirmDialog();
    
    bool shouldDeleteFiles() const;
    int show_modal(); // Returns 1 for Remove, 0 for Cancel
    
private:
    Fl_Check_Button* m_chkDeleteFiles;
    Fl_Button* m_btnRemove;
    Fl_Button* m_btnCancel;
    Fl_Box* m_messageText;
    
    int m_result;
    
    static void onRemove(Fl_Widget* w, void* data);
    static void onCancel(Fl_Widget* w, void* data);
};

#endif // REMOVECONFIRMDIALOG_H
