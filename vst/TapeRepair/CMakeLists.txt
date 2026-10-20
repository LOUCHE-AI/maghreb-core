cmake_minimum_required(VERSION 3.20)

set(SMTG_ADD_VSTGUI ON CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT ON CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)

set(vst3sdk_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sdk")

project(TapeRepair VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "" FORCE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

add_subdirectory(${vst3sdk_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk)
smtg_enable_vst3_sdk()

set(SDK_SRC "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst")
smtg_add_vst3plugin(TapeRepair
    taperepair_vst.cpp
    taperepair_editor.cpp
    taperepair_vst.h
    taperepair_editor.h
    taperepair_dsp.h
    # the repair itself, shared with nothing yet but written to be
    ../shared/tape_restore.cpp
    ../shared/tape_restore.h
    ../shared/wav_io.h
    ../shared/audio_import.cpp
    ../shared/audio_import.h
    ../shared/ui_views.cpp
    ../shared/ui_views.h
    ../shared/frame_open.cpp
    ../shared/frame_open.h
    ../shared/ui_scale.cpp
    ../shared/ui_scale.h
    ${SDK_SRC}/vstsinglecomponenteffect.cpp
    ${SDK_SRC}/vstguieditor.cpp
)

target_include_directories(TapeRepair PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                                              ${CMAKE_CURRENT_SOURCE_DIR}/../shared)
target_link_libraries(TapeRepair PRIVATE sdk vstgui_support)

if(MSVC)
    target_compile_definitions(TapeRepair PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(TapeRepair PRIVATE /utf-8 /we4129)
endif()

if(APPLE)
    set_target_properties(TapeRepair PROPERTIES
        BUNDLE TRUE
        XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++"
        MACOSX_BUNDLE_GUI_IDENTIFIER "fm.wael.taperepair"
        MACOSX_BUNDLE_BUNDLE_NAME "TapeRepair"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    target_compile_options(TapeRepair PRIVATE -Wall -Wno-multichar)
endif()

smtg_target_configure_version_file(TapeRepair)

# ---- CLAP ------------------------------------------------------------------
include(${CMAKE_CURRENT_SOURCE_DIR}/../shared/clap_target.cmake)
add_clap_target(TapeRepair)
