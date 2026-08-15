cmake_minimum_required(VERSION 3.22)

project(OiMusicEditor VERSION 0.1.9 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    message(FATAL_ERROR "JUCE is missing. Clone JUCE 8.0.13 into external/JUCE.")
endif()

add_subdirectory(external/JUCE)

juce_add_gui_app(OiMusicEditor
    PRODUCT_NAME "0i-Studio"
    VERSION "${PROJECT_VERSION}"
    COMPANY_NAME "0i"
    BUNDLE_ID "studio.0i.musiceditor"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/logo.jpg"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/logo.jpg"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "0i-Studio uses audio input when recording is enabled."
)

juce_generate_juce_header(OiMusicEditor)

target_sources(OiMusicEditor PRIVATE
    Source/Main.cpp
    Source/AppState.h
    Source/SpatialParameters.h
    Source/AudioEngine.cpp
    Source/AudioEngine.h
    Source/StudioLookAndFeel.cpp
    Source/StudioLookAndFeel.h
    Source/StudioComponents.cpp
    Source/StudioComponents.h
    Source/UpdateService.cpp
    Source/UpdateService.h
)

target_compile_definitions(OiMusicEditor PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:OiMusicEditor,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
)

target_compile_options(OiMusicEditor PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_link_libraries(OiMusicEditor PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

include(CTest)

if (BUILD_TESTING)
    juce_add_console_app(OiMusicEditorTests
        PRODUCT_NAME "0i-Studio Audio Engine Tests"
    )

    target_sources(OiMusicEditorTests PRIVATE
        Tests/AudioEngineTests.cpp
        Source/AudioEngine.cpp
        Source/AudioEngine.h
        Source/UpdateService.cpp
        Source/UpdateService.h
    )

    juce_generate_juce_header(OiMusicEditorTests)

    target_compile_options(OiMusicEditorTests PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
    )

    target_compile_definitions(OiMusicEditorTests PRIVATE
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
    )

    target_link_libraries(OiMusicEditorTests PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )

    add_test(NAME AudioEngineTests COMMAND OiMusicEditorTests)
endif()
