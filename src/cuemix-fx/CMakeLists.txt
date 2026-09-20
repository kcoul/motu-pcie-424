juce_add_gui_app(CueMixFX
    PRODUCT_NAME                 "CueMix FX"
    BUNDLE_ID                    "space.zenbox.motu.cuemix-fx"
    COMPANY_NAME                 "zenbox"
    VERSION                      ${PROJECT_VERSION}
    ICON_BIG                     "${CMAKE_SOURCE_DIR}/assets/icons/cuemix-fx.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT   "Access to the MOTU PCIe-424 audio inputs.")

target_sources(CueMixFX PRIVATE
    Main.cpp
    ClassicConsole.cpp
    ClassicSkin.cpp
    Console.cpp
    ConsoleModel.cpp
    Meter.cpp
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

# Bundle the Classic sprites so a shipped .app is self-contained.
#
# ClassicSkin::findResources looks inside our own bundle first
# (Contents/Resources/classic), before falling back to whatever CueMix FX the
# user has installed. Without this step that first path never exists, so a
# release build silently drops to the Modern skin on any machine without MOTU's
# app -- which is most machines we would be shipping to.
#
# Copied only if the artwork is present, so a checkout without it still builds;
# the runtime fallback then covers it. See assets/classic/README.md.
set(MOTU_CLASSIC_DIR "${CMAKE_SOURCE_DIR}/assets/classic")
file(GLOB MOTU_CLASSIC_SPRITES "${MOTU_CLASSIC_DIR}/*.png")
if(MOTU_CLASSIC_SPRITES)
    list(LENGTH MOTU_CLASSIC_SPRITES MOTU_CLASSIC_COUNT)
    message(STATUS "CueMix FX: bundling ${MOTU_CLASSIC_COUNT} Classic sprites")
    add_custom_command(TARGET CueMixFX POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_CONTENT_DIR:CueMixFX>/Resources/classic"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MOTU_CLASSIC_SPRITES}
                "$<TARGET_BUNDLE_CONTENT_DIR:CueMixFX>/Resources/classic"
        COMMENT "Bundling Classic skin sprites")
else()
    message(STATUS "CueMix FX: no Classic sprites in assets/classic -- the app "
                   "will read them from an installed CueMix FX, or use Modern")
endif()

# Signing must come after anything that modifies the bundle, or the signature
# covers a tree that no longer matches.
motu_sign(CueMixFX)
