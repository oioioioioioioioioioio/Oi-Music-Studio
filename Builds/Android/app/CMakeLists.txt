cmake_minimum_required(VERSION 3.22)

project(OiStudioAndroid LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(JUCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../external/JUCE")
if (NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE was not found at ${JUCE_DIR}")
endif()

# JUCE's normal CMake entry point bootstraps juceaide with the active generator.
# Android Studio uses an Android/Ninja generator, while juceaide must be a host
# executable.  Reuse a host-built helper when one is available and configure
# only JUCE's modules in the Android tree.
set(OI_JUCE_HOST_TOOLS_DIR "" CACHE PATH
    "Directory containing a host-built juceaide executable for Android builds")

if (OI_JUCE_HOST_TOOLS_DIR STREQUAL "")
    if (CMAKE_HOST_WIN32)
        set(_oi_default_juceaide
            "${CMAKE_CURRENT_LIST_DIR}/../../../build/external/JUCE/tools/extras/Build/juceaide/juceaide_artefacts/Custom/juceaide.exe")
    elseif (CMAKE_HOST_APPLE)
        set(_oi_default_juceaide
            "${CMAKE_CURRENT_LIST_DIR}/../../../build/external/JUCE/tools/extras/Build/juceaide/juceaide_artefacts/Custom/juceaide")
    else()
        set(_oi_default_juceaide
            "${CMAKE_CURRENT_LIST_DIR}/../../../build/external/JUCE/tools/extras/Build/juceaide/juceaide_artefacts/Custom/juceaide")
    endif()

    if (EXISTS "${_oi_default_juceaide}")
        set(OI_JUCE_HOST_TOOLS_DIR
            "${CMAKE_CURRENT_LIST_DIR}/../../../build/external/JUCE/tools/extras/Build/juceaide/juceaide_artefacts/Custom"
            CACHE PATH "Directory containing a host-built juceaide executable" FORCE)
    endif()
endif()

if (NOT OI_JUCE_HOST_TOOLS_DIR STREQUAL "")
    if (CMAKE_HOST_WIN32)
        set(_oi_juceaide_name "juceaide.exe")
    else()
        set(_oi_juceaide_name "juceaide")
    endif()
    set(_oi_juceaide_path "${OI_JUCE_HOST_TOOLS_DIR}/${_oi_juceaide_name}")

    if (NOT EXISTS "${_oi_juceaide_path}")
        message(FATAL_ERROR
            "OI_JUCE_HOST_TOOLS_DIR was set, but juceaide was not found at ${_oi_juceaide_path}")
    endif()

    set(JUCE_MODULES_ONLY ON CACHE BOOL "Only configure JUCE modules" FORCE)
    add_subdirectory("${JUCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/juce")

    # JUCE_MODULES_ONLY skips JUCEUtils, so load it explicitly and provide the
    # imported host tool that its code-generation functions invoke.
    include("${JUCE_DIR}/extras/Build/CMake/JUCEUtils.cmake")
    if (NOT TARGET juce::juceaide)
        add_executable(juce::juceaide IMPORTED GLOBAL)
        set_target_properties(juce::juceaide PROPERTIES
            IMPORTED_LOCATION "${_oi_juceaide_path}")
    endif()
else()
    message(STATUS
        "No host juceaide found; JUCE will bootstrap it with the active CMake generator. "
        "Build the desktop target first or set -DOI_JUCE_HOST_TOOLS_DIR=<path> if configuration fails.")
    add_subdirectory("${JUCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/juce")
endif()

# JUCE's Android activity loads this exact shared-library name.
juce_add_gui_app(juce_jni
    PRODUCT_NAME "0i-Studio"
    VERSION "0.1.10"
    COMPANY_NAME "0i"
    BUNDLE_ID "studio.oi.musiceditor"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "0i-Studio uses the microphone only when recording is enabled."
)

juce_generate_juce_header(juce_jni)

target_sources(juce_jni PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../Source/Main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../../Source/AppState.h"
    "${CMAKE_CURRENT_LIST_DIR}/../../../Source/SpatialParameters.h"
    "${CMAKE_CURRENT_LIST_DIR}/../../../Source/AudioEngine.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../../Source/AudioEngine.h"
    "${CMAKE_CURRENT_LIST_DIR}/../../../Source/StudioLookAndFeel.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../../Source/StudioLookAndFeel.h"
    "${CMAKE_CURRENT_LIST_DIR}/../../../Source/StudioComponents.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../../Source/StudioComponents.h"
    "${CMAKE_CURRENT_LIST_DIR}/../../../Source/UpdateService.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../../Source/UpdateService.h"
)

target_compile_definitions(juce_jni PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_PUSH_NOTIFICATIONS=1
    JUCE_PUSH_NOTIFICATIONS_ACTIVITY="studio/oi/musiceditor/OiJuceActivity"
    JUCE_CONTENT_SHARING=1
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:juce_jni,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="0.1.10"
)

target_compile_options(juce_jni PRIVATE
    -Wall -Wextra -Wno-unused-parameter -fno-omit-frame-pointer
    $<$<PLATFORM_ID:Android>:-Wno-unguarded-availability>
    $<$<PLATFORM_ID:Android>:-Wno-error=unguarded-availability>
)

# The Windows-hosted NDK 28 Clang frontend can crash when several large JUCE
# translation units compile concurrently, especially for armeabi-v7a.
if (CMAKE_GENERATOR MATCHES "Ninja")
    set_property(GLOBAL PROPERTY JOB_POOLS oi_android_compile_pool=1)
    set_property(TARGET juce_jni PROPERTY JOB_POOL_COMPILE oi_android_compile_pool)
endif()

target_link_libraries(juce_jni PRIVATE
    juce::juce_audio_utils
    juce::juce_cryptography
    juce::juce_dsp
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
