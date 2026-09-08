juce_add_gui_app(PCIAudioSetup
    PRODUCT_NAME                 "MOTU PCI Audio Setup"
    BUNDLE_ID                    "space.zenbox.motu.pci-audio-setup"
    COMPANY_NAME                 "zenbox"
    VERSION                      ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT   "Access to the MOTU PCIe-424 audio inputs.")

target_sources(PCIAudioSetup PRIVATE
    Main.cpp
    MainComponent.cpp)

target_compile_definitions(PCIAudioSetup PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:PCIAudioSetup,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:PCIAudioSetup,JUCE_VERSION>")

target_link_libraries(PCIAudioSetup PRIVATE
    motu_card
    ${MOTU_JUCE_MODULES}
    ${MOTU_JUCE_FLAGS})

motu_sign(PCIAudioSetup)
