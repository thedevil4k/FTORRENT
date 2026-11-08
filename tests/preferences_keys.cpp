// PreferencesDialog key persistence checks.
//
// The Security tab has one "Save key" button per provider, but a user who
// pastes a key and presses OK (or Apply) expects the key to be stored too.
// These checks drive the real dialog: they locate the two secret fields, fill
// them, press the real button and read the key back through SecurityKeys. The
// empty-field case matters as much as the others: accepting the dialog must
// never erase a saved key just because the field was blank.
#include "PreferencesDialog.h"
#include "SecurityKeys.h"
#include "test_support.h"

#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Secret_Input.H>
#include <FL/Fl_Window.H>

#include <cstdio>
#include <cstring>
#include <string>
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

// The Security tab creates the VirusTotal field before the MalwareBazaar one,
// so child order is the order the user sees on screen.
void collectSecrets(Fl_Widget* widget, std::vector<Fl_Secret_Input*>& out) {
    if (auto* secret = dynamic_cast<Fl_Secret_Input*>(widget)) {
        out.push_back(secret);
    }
    if (Fl_Group* group = widget->as_group()) {
        for (int i = 0; i < group->children(); ++i) {
            collectSecrets(group->child(i), out);
        }
    }
}

Fl_Button* button(PreferencesDialog& dialog, const char* label) {
    return dynamic_cast<Fl_Button*>(byLabel(&dialog, label));
}

// fl_message() runs its own modal loop. The window that loop waits on is
// Fl::modal(); hiding a window from Fl::first_window() can miss it and leave
// the test blocked forever. awake() makes wait() return so the loop can see
// shown() == 0 without waiting for the next native event.
void dismissMessage(void*) {
    if (Fl_Window* modal = Fl::modal()) {
        modal->hide();
        Fl::awake();
    }
}

std::string stored(SecurityKeys::Provider provider) {
    std::string key;
    SecurityKeys::load(provider, key);
    return key;
}

} // namespace

int main() {
    // A blocking modal keeps the process alive; unbuffered output says which
    // section reached it even when the harness kills the run on a timeout.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    testsupport::useScratchConfig("preferences_keys");

    const SecurityKeys::Provider vt = SecurityKeys::Provider::VirusTotal;
    const SecurityKeys::Provider mb = SecurityKeys::Provider::MalwareBazaar;

    testsupport::section("OK stores what was pasted");
    {
        PreferencesDialog dialog;
        testsupport::forceLayout(&dialog);

        std::vector<Fl_Secret_Input*> fields;
        collectSecrets(&dialog, fields);
        testsupport::ok(fields.size() == 2,
                        "the Security tab has two secret fields");
        Fl_Button* ok = button(dialog, "OK");
        testsupport::ok(ok != nullptr, "the dialog has an OK button");
        if (fields.size() != 2 || !ok) {
            testsupport::removeScratchConfig("preferences_keys");
            return testsupport::report("preferences_keys");
        }

        // Paste noise is what a browser or an email adds; it is not the key.
        fields[0]->value("  vt-key-1234567890\n");
        fields[1]->value(" mb-key-1234567890\t");
        ok->do_callback();

        testsupport::ok(stored(vt) == "vt-key-1234567890",
                        "OK stored the VirusTotal key without the paste noise");
        testsupport::ok(stored(mb) == "mb-key-1234567890",
                        "OK stored the MalwareBazaar key too");
    }

    testsupport::section("Apply stores the fields as well");
    {
        PreferencesDialog dialog;
        testsupport::forceLayout(&dialog);

        std::vector<Fl_Secret_Input*> fields;
        collectSecrets(&dialog, fields);
        Fl_Button* apply = button(dialog, "Apply");
        testsupport::ok(fields.size() == 2 && apply != nullptr,
                        "the dialog has two fields and an Apply button");
        if (fields.size() == 2 && apply) {
            fields[0]->value("vt-applied");
            fields[1]->value("mb-applied");

            Fl::add_timeout(0.2, dismissMessage, nullptr);
            apply->do_callback();
            Fl::remove_timeout(dismissMessage, nullptr);

            testsupport::ok(stored(vt) == "vt-applied",
                            "Apply stored the VirusTotal key");
            testsupport::ok(stored(mb) == "mb-applied",
                            "Apply stored the MalwareBazaar key");
        }
    }

    testsupport::section("an empty field never erases a saved key");
    {
        PreferencesDialog dialog;
        testsupport::forceLayout(&dialog);

        std::vector<Fl_Secret_Input*> fields;
        collectSecrets(&dialog, fields);
        Fl_Button* ok = button(dialog, "OK");
        if (fields.size() == 2 && ok) {
            // The dialog loaded the saved keys into the fields; blanking them
            // is what a user does before typing a new one, not a request to
            // remove the old one. "Remove key" is the explicit way to do that.
            fields[0]->value("");
            fields[1]->value("");
            ok->do_callback();
        }

        testsupport::ok(SecurityKeys::exists(vt),
                        "OK with an empty field kept the VirusTotal key");
        testsupport::ok(SecurityKeys::exists(mb),
                        "OK with an empty field kept the MalwareBazaar key");
    }

    testsupport::removeScratchConfig("preferences_keys");
    return testsupport::report("preferences_keys");
}
