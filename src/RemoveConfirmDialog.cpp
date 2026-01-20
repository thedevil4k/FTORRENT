#include "RemoveConfirmDialog.h"
#include <FL/Fl.H>
#include <string>

RemoveConfirmDialog::RemoveConfirmDialog(int count)
    : Fl_Double_Window(300, 130, "Remove Torrent(s)")
    , m_result(0)
{
    // Text
    m_messageText = new Fl_Box(20, 15, 260, 40);
    std::string msg = "Are you sure you want to remove " + std::to_string(count) + " selected torrent(s)?";
    m_messageText->copy_label(msg.c_str());
    m_messageText->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);

    // Checkbox
    m_chkDeleteFiles = new Fl_Check_Button(20, 60, 260, 25, "Also delete downloaded files");
    m_chkDeleteFiles->down_box(FL_DOWN_BOX);

    // Buttons
    m_btnRemove = new Fl_Button(110, 95, 80, 25, "Remove");
    m_btnRemove->callback(onRemove, this);
    
    m_btnCancel = new Fl_Button(200, 95, 80, 25, "Cancel");
    m_btnCancel->callback(onCancel, this);
    
    end();
    set_modal();
}

RemoveConfirmDialog::~RemoveConfirmDialog() {
}

bool RemoveConfirmDialog::shouldDeleteFiles() const {
    return m_chkDeleteFiles->value() != 0;
}

int RemoveConfirmDialog::show_modal() {
    show();
    while (shown()) {
        Fl::wait();
    }
    return m_result;
}

void RemoveConfirmDialog::onRemove(Fl_Widget* w, void* data) {
    RemoveConfirmDialog* self = (RemoveConfirmDialog*)data;
    self->m_result = 1;
    self->hide();
}

void RemoveConfirmDialog::onCancel(Fl_Widget* w, void* data) {
    RemoveConfirmDialog* self = (RemoveConfirmDialog*)data;
    self->m_result = 0;
    self->hide();
}
