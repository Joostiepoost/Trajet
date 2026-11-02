# Trajet — automation score app
# Build instructions: see README.md (short version: ./build.sh)

cmake_minimum_required(VERSION 3.22)

# Apple Silicon only, macOS 12 or newer.
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "")

project(Trajet VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# --- JUCE --------------------------------------------------------------------
# By default JUCE is downloaded automatically (first build takes a few minutes).
# Set TRAJET_JUCE_PATH to use a local copy instead.
set(TRAJET_JUCE_PATH "" CACHE PATH "Optional path to a local JUCE checkout")

if(TRAJET_JUCE_PATH)
    add_subdirectory(${TRAJET_JUCE_PATH} JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# --- The app -----------------------------------------------------------------
juce_add_gui_app(Trajet
    PRODUCT_NAME        "Trajet"
    COMPANY_NAME        "Trajet"
    BUNDLE_ID           "com.trajet.app"
    VERSION             ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED FALSE)

juce_generate_juce_header(Trajet)

target_sources(Trajet PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/PluginHost.cpp
    Source/ParameterListComponent.cpp
    Source/Model/Project.cpp
    Source/Engine/AutomationEngine.cpp
    Source/Engine/AutomationController.cpp
    Source/UI/ScoreView.cpp
    Source/UI/LaneTrack.cpp
    Source/UI/Inspector.cpp
    Source/UI/ItemEditor.cpp
    Source/Model/PointOps.cpp
    Source/UI/TransportBar.cpp)

target_compile_definitions(Trajet PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Trajet,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Trajet,JUCE_VERSION>"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LADSPA=0)

if(APPLE)
    target_compile_definitions(Trajet PRIVATE JUCE_PLUGINHOST_AU=1)
endif()

target_link_libraries(Trajet
    PRIVATE
        juce::juce_gui_extra
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
