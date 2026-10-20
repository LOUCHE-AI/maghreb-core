# One CLAP target per plug-in, built from the VST3 target's own source list.
#
# Nothing here is per plug-in except the name: the format lives in
# shared/clap_vst3.cpp and the plug-in's clap_entry.cpp says what it is called.
# The SDK's hosting helpers come along because this module does host a VST3
# component - its own - so it needs the parts of the SDK that a host uses.
function(add_clap_target vst3target)
    set(_sdk "${CMAKE_CURRENT_SOURCE_DIR}/../sdk")
    get_target_property(_src ${vst3target} SOURCES)

    add_library(${vst3target}CLAP MODULE
        ${_src}
        ${CMAKE_CURRENT_SOURCE_DIR}/clap_entry.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared/clap_vst3.cpp
        ${_sdk}/public.sdk/source/vst/hosting/hostclasses.cpp
        ${_sdk}/public.sdk/source/vst/hosting/eventlist.cpp
        ${_sdk}/public.sdk/source/vst/hosting/parameterchanges.cpp
        ${_sdk}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
        ${_sdk}/public.sdk/source/common/memorystream.cpp)

    set_target_properties(${vst3target}CLAP PROPERTIES
        PREFIX "" OUTPUT_NAME "${vst3target}" SUFFIX ".clap")
    target_include_directories(${vst3target}CLAP PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared)
    target_link_libraries(${vst3target}CLAP PRIVATE sdk vstgui_support)
endfunction()
