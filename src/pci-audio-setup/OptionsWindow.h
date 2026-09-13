#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "motu_card.h"

#include <functional>
#include <memory>

// "<Interface> Options" — one small fixed window per interface, laid out from
// docs/reference/setup-options-{hd192,24io,2408mk3}.png. Which controls appear
// is decided by which OtherInterfaceOp selectors the interface answers, not by
// its name (docs/CHANNEL-STATE.md).
namespace OptionsWindow {

std::unique_ptr<juce::DocumentWindow> create(juce::Component& owner, motu::Card card, int wire,
                                             std::function<void()> onClose);

}  // namespace OptionsWindow
