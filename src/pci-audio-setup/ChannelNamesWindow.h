#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "motu_card.h"

#include <functional>
#include <memory>

// "MOTU Channel Names": custom names for every active input and output, the
// job of MOTU's bundled helper app, which is i386 and cannot run on Sequoia.
//
// Names are written with SetCustomChannelNameCFString; CoreAudio shows them at
// once (as the channel's category name) and the driver keeps them in the per-OS
// prefs file. An empty name restores the hardware name. "Import Names..." copies
// names from another boot volume's prefs file, e.g. the Mojave install.
namespace ChannelNamesWindow {

std::unique_ptr<juce::DocumentWindow> create(juce::Component& owner, motu::Card card,
                                             std::function<void()> onNamesChanged,
                                             std::function<void()> onClose);

}  // namespace ChannelNamesWindow
