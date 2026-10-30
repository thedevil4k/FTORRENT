#ifndef SECURITYKEYS_H
#define SECURITYKEYS_H

#include <string>

/**
 * @brief Storage for the API keys the reputation providers need.
 *
 * Keys never go into settings.ini: that file is plain text, is copied around by
 * every backup tool and is the first thing anyone reads when something goes
 * wrong. Instead each key gets a file of its own, protected by the operating
 * system:
 *
 *   Windows  DPAPI (CryptProtectData). The blob is bound to the Windows user
 *            account, so copying the file to another machine does not give the
 *            key to anyone.
 *   Linux    a file in a 0700 directory, created 0600 with open(), so the key
 *            is readable only by the user running FTorrent.
 *
 * Neither path encrypts against a process running as the same user -- nothing
 * at this privilege level can -- and the class does not pretend otherwise.
 */
class SecurityKeys {
public:
    // Every provider that needs a key appears here. Both current ones do:
    // VirusTotal wants an x-apikey, MalwareBazaar an Auth-Key (it answers 401
    // without one, which a live check found).
    enum class Provider {
        VirusTotal,
        MalwareBazaar
    };

    // Stable identifier, also used as the file name.
    static const char* providerId(Provider provider);

    // Stores the key, replacing any previous one. An empty key removes it.
    // Returns false and fills 'error' when the key cannot be written safely.
    static bool save(Provider provider, const std::string& key,
                     std::string& error);

    // Reads the key into 'key'. Returns false when there is none or when the
    // stored blob cannot be decrypted by this user.
    static bool load(Provider provider, std::string& key);

    static bool remove(Provider provider);

    static bool exists(Provider provider);

    // Where and how the key is kept, for the Preferences dialog. Describes the
    // mechanism, never the value.
    static std::string storageDescription();

    // Full path of the file backing the key. Empty when unavailable.
    static std::string storagePath(Provider provider);
};

#endif // SECURITYKEYS_H
