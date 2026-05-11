# cmake/clap_juce_extensions.cmake
# Phase 21 / Task 2: FetchContent shim for free-audio/clap-juce-extensions.
#
# JUCE 8 has no native CLAP support; JUCE 9 will but has no release date.
# This shim pulls in the MIT-licensed clap-juce-extensions project which
# layers a CLAP wrapper on top of a JUCE plugin target. Swap path for the
# future JUCE-9-native-CLAP move: delete this include, add CLAP to FORMATS,
# remove the clap_juce_extensions_plugin() call below the juce_add_plugin().
#
# Pinned by SHA per project FetchContent discipline (root CMakeLists.txt
# pins JUCE the same way). SHA resolved 2026-05-11 via:
#   git ls-remote https://github.com/free-audio/clap-juce-extensions.git HEAD

include(FetchContent)

FetchContent_Declare(
    clap-juce-extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG        e8de9e8571626633b8541a54c2406fccc4272767
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(clap-juce-extensions)

# clap_juce_extensions_plugin() is provided by the fetched project and
# attaches a *_CLAP target to the named JUCE plugin target.
