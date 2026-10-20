cmake_minimum_required(VERSION 3.20)

# VSTGUI is needed for the editor - the degree ruler is the whole reason this
# has a window rather than a generic parameter list.
set(SMTG_ADD_VSTGUI ON CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT ON CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)

set(vst3sdk_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sdk")

project(TubuDX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- macOS ----------------------------------------------------------------
#
# Set before the SDK is added, because the SDK reads these when it builds its
# own targets: a universal plug-in linked against a single-architecture SDK
# will not link at all.
#
# 10.13 is what the VST3 SDK itself asks for. Both architectures are built so
# one bundle runs on Apple silicon and on Intel; drop x86_64 if that is not
# wanted, but do it here rather than per target.
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

add_subdirectory(${vst3sdk_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk)
smtg_enable_vst3_sdk()

# SingleComponentEffect and VSTGUIEditor are not part of the `sdk` library
# target, so they are compiled into the plug-in directly.
set(SDK_SRC "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst")
smtg_add_vst3plugin(TubuDX
    tubudx_vst.cpp
    tubudx_editor.cpp
    tubudx_vst.h
    tubudx_editor.h
    tubudx_dsp.h
    tubu_tuning.h
    dx_sysex.h
    dx_engine.h
    dx_engine.cpp
    # Dexed's DSP core, Apache 2.0. Only the msfa folder is vendored; Dexed's
    # own application code is GPL-3.0 and is deliberately not included.
    msfa/dx7note.cc
    msfa/env.cc
    msfa/exp2.cc
    msfa/fm_core.cc
    msfa/fm_op_kernel.cc
    msfa/freqlut.cc
    msfa/lfo.cc
    msfa/pitchenv.cc
    msfa/sin.cc
    msfa/porta.cpp
    msfa/tuning.cc
    # the cartridge browser, shared with Maghreb DX and Maghreb Seq
    ../shared/cart_panel.cpp
    ../shared/ui_views.cpp
    ../shared/ui_views.h
    ../shared/frame_open.cpp
    ../shared/frame_open.h
    ../shared/ui_scale.cpp
    ../shared/ui_scale.h
    ../shared/cart_panel.h
    ../shared/cart_index.h
    ${SDK_SRC}/vstsinglecomponenteffect.cpp
    ${SDK_SRC}/vstguieditor.cpp
)

target_include_directories(TubuDX PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                                        ${CMAKE_CURRENT_SOURCE_DIR}/../shared)
target_link_libraries(TubuDX PRIVATE sdk vstgui_support)

if(MSVC)
    target_compile_definitions(TubuDX PRIVATE _CRT_SECURE_NO_WARNINGS)
    # the tabʿ names carry Arabic transliteration, so the sources are UTF-8
    # /we4129 turns "unrecognized character escape sequence" into an
    # error. A single backslash in a Windows path literal is only a
    # warning by default, and it cost an afternoon.
    target_compile_options(TubuDX PRIVATE /utf-8 /we4129)
endif()

if(APPLE)
    # A .vst3 on macOS is a bundle, and a bundle without these is rejected by
    # the host before any of its code runs. The identifier has to be unique per
    # plug-in or two of them will collide in the same session.
    set_target_properties(TubuDX PROPERTIES
        BUNDLE TRUE
        XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++"
        MACOSX_BUNDLE_GUI_IDENTIFIER "fm.wael.tubudx"
        MACOSX_BUNDLE_BUNDLE_NAME "TubuDX"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    target_compile_options(TubuDX PRIVATE -Wall -Wno-multichar)
endif()

smtg_target_configure_version_file(TubuDX)

# ---- CLAP ------------------------------------------------------------------
include(${CMAKE_CURRENT_SOURCE_DIR}/../shared/clap_target.cmake)
add_clap_target(TubuDX)
