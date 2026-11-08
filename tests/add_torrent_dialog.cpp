// The "Check archive" button of the Add Torrent dialog.
//
// The button used to follow only the check boxes. Fl_Check_Browser toggles a
// check box when a row is selected, so clicking the (checked) .zip/.rar to look
// at it unchecked it and disabled the button: the one gesture the user makes to
// choose the archive was the gesture that made the check impossible. These
// checks pin the intended behaviour: an archive anywhere in the list is enough,
// and the highlighted row is the target even after the click unchecked it.
#include "AddTorrentDialog.h"
#include "TorrentFileList.h"
#include "test_support.h"

#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Browser.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Widget.H>

#include <cstring>
#include <vector>

namespace {

Fl_Widget* byLabel(Fl_Widget* widget, const char* label) {
    if (widget->label() && std::strcmp(widget->label(), label) == 0) {
        return widget;
    }
    if (Fl_Group* group = widget->as_group()) {
        for (int i = 0; i < group->children(); ++i) {
            if (Fl_Widget* found = byLabel(group->child(i), label)) {
                return found;
            }
        }
    }
    return nullptr;
}

Fl_Check_Browser* checkBrowser(Fl_Widget* widget) {
    if (auto* browser = dynamic_cast<Fl_Check_Browser*>(widget)) return browser;
    if (Fl_Group* group = widget->as_group()) {
        for (int i = 0; i < group->children(); ++i) {
            if (Fl_Check_Browser* found = checkBrowser(group->child(i))) {
                return found;
            }
        }
    }
    return nullptr;
}

TorrentFileEntry file(int index, const char* path, bool executable = false) {
    TorrentFileEntry entry;
    entry.fileIndex = index;
    entry.path = path;
    entry.size = 1024;
    entry.executable = executable;
    return entry;
}

} // namespace

int main() {
    testsupport::useScratchConfig("add_torrent_archive");

    {
        AddTorrentDialog dialog;
        testsupport::forceLayout(&dialog);

        Fl_Check_Browser* browser = checkBrowser(&dialog);
        Fl_Button* check =
            dynamic_cast<Fl_Button*>(byLabel(&dialog, "Check archive"));
        testsupport::ok(browser != nullptr, "the dialog has a file browser");
        testsupport::ok(check != nullptr,
                        "the dialog has a Check archive button");
        if (!browser || !check) {
            testsupport::removeScratchConfig("add_torrent_archive");
            return testsupport::report("add_torrent_dialog");
        }

        const TorrentFileEntry exe = file(0, "crack/rufus-4.1.exe", true);
        const TorrentFileEntry zip = file(1, "crack/tools.zip");
        const TorrentFileEntry rar = file(2, "crack/data.rar");

        dialog.setFileList({exe, zip, rar});
        testsupport::forceLayout(&dialog);
        testsupport::ok(check->active(),
                        "a checked .zip enables the button without a click");

        // What a click on the .zip row does: select it (which unchecks it) and
        // run the callback the dialog registered.
        browser->select(browser->item_at(2), 1, 1);
        testsupport::forceLayout(&dialog);
        testsupport::ok(!browser->checked(2),
                        "selecting a checked row toggles its check box");
        testsupport::ok(check->active(),
                        "the highlighted .zip keeps the button enabled");
        testsupport::ok(browser->value() == 2,
                        "the clicked row is the one the button targets");

        // A .rar deeper in the list works too, even when it is not checked.
        browser->select(browser->item_at(3), 1, 1);
        testsupport::forceLayout(&dialog);
        testsupport::ok(check->active(),
                        "a highlighted .rar keeps the button enabled");

        dialog.setFileList({exe});
        testsupport::forceLayout(&dialog);
        testsupport::ok(!check->active(),
                        "a list without .zip/.rar disables the button");
    }

    testsupport::removeScratchConfig("add_torrent_archive");
    return testsupport::report("add_torrent_dialog");
}
