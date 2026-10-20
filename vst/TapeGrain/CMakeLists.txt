cmake_minimum_required(VERSION 3.20)

# VSTGUI is needed for the editor: the waveform, the knobs and - the reason it
# is worth the build time - the native file dialog behind Load Sample.
set(SMTG_ADD_VSTGUI ON CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT ON CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)

set(vst3sdk_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sdk")

project(TapeGrain VERSION 0.2.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- macOS ----------------------------------------------------------------
#
# Set before the SDK is added, because the SDK reads these when it builds its
# own targets: a universal plug-in linked against a single-architecture SDK
# will not link at all.
#
# 10.15, not the SDK's own 10.13: std::filesystem needs it. Both architectures are built so
# one bundle runs on Apple silicon and on Intel; drop x86_64 if that is not
# wanted, but do it here rather than per target.
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "" FORCE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

add_subdirectory(${vst3sdk_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk)
smtg_enable_vst3_sdk()

# SingleComponentEffect and the pieces it leans on are not part of the `sdk`
# library target, so they are compiled into the plug-in directly. Without
# these the build compiles cleanly and then fails at link with a wall of
# unresolved Steinberg symbols. vstguieditor.cpp is in the same position.
set(SDK_SRC "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst")
smtg_add_vst3plugin(TapeGrain
    tapegrain_vst.cpp
    tapegrain_editor.cpp
    tapegrain_vst.h
    tapegrain_editor.h
    tapegrain_dsp.h
    tapegrain_io.h
    tapegrain_separate.cpp
    tapegrain_separate.h
    # this one alone: it includes the bus, parameter and edit-controller
    # sources itself, so adding them as well gives duplicate symbols
    ../shared/ui_views.cpp
    ../shared/ui_views.h
    ../shared/frame_open.cpp
    ../shared/frame_open.h
    ../shared/ui_scale.cpp
    ../shared/ui_scale.h
    ${SDK_SRC}/vstsinglecomponenteffect.cpp
    ${SDK_SRC}/vstguieditor.cpp
)

target_include_directories(TapeGrain PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                                        ${CMAKE_CURRENT_SOURCE_DIR}/../shared)
target_link_libraries(TapeGrain PRIVATE sdk vstgui_support)

if(MSVC)
    target_compile_definitions(TapeGrain PRIVATE _CRT_SECURE_NO_WARNINGS)
    # /we4129 turns "unrecognized character escape sequence" into an error. A
    # single backslash in a Windows path literal is only a warning by default,
    # and it cost an afternoon.
    target_compile_options(TapeGrain PRIVATE /we4129)
endif()

if(APPLE)
    # A .vst3 on macOS is a bundle, and a bundle without these is rejected by
    # the host before any of its code runs. The identifier has to be unique per
    # plug-in or two of them will collide in the same session.
    set_target_properties(TapeGrain PROPERTIES
        BUNDLE TRUE
        XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++"
        MACOSX_BUNDLE_GUI_IDENTIFIER "fm.wael.tapegrain"
        MACOSX_BUNDLE_BUNDLE_NAME "TapeGrain"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    target_compile_options(TapeGrain PRIVATE -Wall -Wno-multichar)
endif()

smtg_target_configure_version_file(TapeGrain)


# ---- CLAP ------------------------------------------------------------------
include(${CMAKE_CURRENT_SOURCE_DIR}/../shared/clap_target.cmake)
add_clap_target(TapeGrain)
