// Reading MOTU's per-OS driver preference files.
//
//   ~/Library/Preferences/com.motu.PCIAudio/PCI-424.bus<N>.slot0.plist
//
// These are what another boot volume's custom channel names and settings live
// in (docs/CHANNEL-STATE.md). Reading only: the driver owns writing them.

#pragma once

#include <string>
#include <vector>

namespace motu::prefs {

struct ChannelNames {
    std::vector<std::string> inputs, outputs;   // indexed by channel id; "" = unnamed
};

// InputNames / OutputNames are UTF-16LE <data> blobs with a BOM, not strings,
// which is why a plain plist read shows them empty. Returns false and fills
// `err` if the file cannot be read or has neither key.
bool readChannelNames(const std::string& path, ChannelNames& out, std::string* err = nullptr);

// Every PCI-424 driver prefs file under the mounted volumes' home folders and
// this user's own, newest first. The bus number in the name follows the slot.
std::vector<std::string> findPrefsFiles();

}  // namespace motu::prefs
