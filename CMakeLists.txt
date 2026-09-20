cmake_minimum_required(VERSION 3.22)

project(motu-pcie-424 VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# motu_card.mm is Objective-C++, and CMAKE_CXX_STANDARD does not reach it.
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

# The HAL plugin is loaded into our process and ships i386 + x86_64 only, so the
# apps must be x86_64. Native on this machine (Intel), and pinned so a build on
# anything else cannot silently produce a binary the plugin will not load into.
set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "")

add_subdirectory(third_party/JUCE)

# Shared card access. Deliberately independent of JUCE so it stays testable on
# its own (see src/common/motu_dump.mm, built by tools/build.sh).
add_library(motu_card STATIC src/common/motu_card.mm src/common/motu_prefs.mm)
target_include_directories(motu_card PUBLIC src/common)
target_link_libraries(motu_card PUBLIC
    "-framework CoreAudio"
    "-framework CoreFoundation"
    "-framework Foundation")

# Every app links this. Note what is NOT here: juce_audio_devices. These are
# control surfaces, not audio clients, and whichever thread reaches the
# CoreAudio HAL first wins the run-loop registration that the 'Mapi' card
# pointer depends on. Leaving the module out makes that a link error rather
# than a mystery at runtime. See the threading note in src/common/motu_card.h.
set(MOTU_JUCE_MODULES
    juce::juce_gui_basics
    juce::juce_graphics
    juce::juce_data_structures
    juce::juce_events
    juce::juce_core)

set(MOTU_JUCE_FLAGS
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# Sign with the audio-input entitlement. Without it the HAL plugin hands back a
# NULL card pointer, and JUCE's own bundling step does not cover this.
#
# Prefer a real certificate over ad-hoc: ad-hoc signing changes the cdhash on
# every build, so TCC treats each rebuild as a new app and re-prompts for the
# microphone. Re-run cmake after adding a certificate to pick it up.
#
# Selection is delegated to tools/sign-identity.sh, shared with tools/build.sh.
# Do NOT inline "the first Apple Development cert" here again: this keychain has
# certs from several teams, and the one that sorts first produces apps that will
# not launch at all (Launchd job spawn failed, POSIX 162). The script documents
# it.
execute_process(
    COMMAND "${CMAKE_SOURCE_DIR}/tools/sign-identity.sh"
    OUTPUT_VARIABLE MOTU_SIGN_ID ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT MOTU_SIGN_ID)
    set(MOTU_SIGN_ID "-")
endif()
if(MOTU_SIGN_ID STREQUAL "-")
    message(STATUS "Code signing identity: ad-hoc (microphone permission will "
                   "be re-requested on every build)")
else()
    message(STATUS "Code signing identity: ${MOTU_SIGN_ID}")
endif()

# Release builds are signed for distribution: a Developer ID identity, hardened
# runtime and a secure timestamp, all three of which notarization requires.
#
# Hardened runtime is the dangerous one. It turns on library validation, which
# is precisely what stops MOTU's own CueMix FX from loading the 2017 PCI
# HALPlugin (docs/ORIGINAL-UI.md). tools/entitlements.plist carries
# com.apple.security.cs.disable-library-validation to compensate, and that pair
# was verified against the card on 2026-09-19.
#
#   cmake -B build/release -DCMAKE_BUILD_TYPE=Release -DMOTU_RELEASE_SIGN=ON \
#         -DMOTU_SIGN_IDENTITY="Developer ID Application: ... (TEAMID)"
#
# Default OFF, so ordinary development builds keep signing exactly as before --
# no hardened runtime, so no library validation to work around.
option(MOTU_RELEASE_SIGN "Sign for distribution: hardened runtime + timestamp" OFF)

if(MOTU_RELEASE_SIGN)
    set(MOTU_SIGN_EXTRA --options runtime --timestamp)
    if(NOT MOTU_SIGN_ID MATCHES "Developer ID")
        message(WARNING
            "MOTU_RELEASE_SIGN is ON but the identity is '${MOTU_SIGN_ID}'. "
            "Notarization needs a 'Developer ID Application' certificate; an "
            "Apple Development one will not launch on other machines.")
    endif()
else()
    set(MOTU_SIGN_EXTRA "")
endif()

function(motu_sign target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND codesign -f -s "${MOTU_SIGN_ID}" ${MOTU_SIGN_EXTRA} --entitlements
                "${CMAKE_SOURCE_DIR}/tools/entitlements.plist"
                "$<TARGET_BUNDLE_DIR:${target}>"
        COMMENT "Signing ${target}"
        VERBATIM)
endfunction()

add_subdirectory(src/pci-audio-setup)
add_subdirectory(src/cuemix-fx)
