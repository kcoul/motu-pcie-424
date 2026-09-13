juce_add_gui_app(CueMixFX
    PRODUCT_NAME                 "CueMix FX"
    BUNDLE_ID                    "space.zenbox.motu.cuemix-fx"
    COMPANY_NAME                 "zenbox"
    VERSION                      ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT   "Access to the MOTU PCIe-424 audio inputs.")

target_sources(CueMixFX PRIVATE
    Main.cpp
    Console.cpp
    Strip.cpp)

target_compile_definitions(CueMixFX PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:CueMixFX,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:CueMixFX,JUCE_VERSION>")

target_link_libraries(CueMixFX PRIVATE
    motu_card
    ${MOTU_JUCE_MODULES}
    ${MOTU_JUCE_FLAGS})

motu_sign(CueMixFX)
