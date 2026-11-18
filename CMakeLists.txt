# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the LV2 bundle on any platform, plus the VST3 plugin (Windows) and the Audio Unit
# (macOS). On Windows, build.bat builds the LV2 bundle without CMake.
cmake_minimum_required(VERSION 3.16)
project(schallmauer VERSION 1.0.0 LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# The extra formats download their SDKs while configuring (both permissively licensed; see
# NOTICE.md). The VST3 SDK needs CMake 3.25 or later.
option(SCHALLMAUER_VST3 "Build the VST3 plugin (fetches the Steinberg VST3 SDK)" ${WIN32})
option(SCHALLMAUER_AU "Build the Audio Unit (macOS; fetches the Apple AudioUnitSDK)" ${APPLE})

if(WIN32)
    set(LIB_SUFFIX .dll)
    set(UI_CLASS ui:WindowsUI)
    set(WITH_UI ON)
elseif(APPLE)
    set(LIB_SUFFIX .dylib)
    set(UI_CLASS ui:CocoaUI)
    set(WITH_UI ON)
    enable_language(OBJC) # the Cocoa window layer in view.c
    set_source_files_properties(src/ui/view.c PROPERTIES LANGUAGE OBJC)
else()
    set(LIB_SUFFIX .so)
    set(UI_CLASS ui:X11UI)
    find_package(X11)
    set(WITH_UI ${X11_FOUND})
endif()

set(BUNDLE_DIR ${CMAKE_BINARY_DIR}/schallmauer.lv2)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

add_library(schallmauer MODULE src/plugin.c src/engine.c src/amp.c)
set_target_properties(schallmauer PROPERTIES
    PREFIX ""
    SUFFIX ${LIB_SUFFIX}
    C_STANDARD 11
    C_VISIBILITY_PRESET hidden
    LIBRARY_OUTPUT_DIRECTORY "$<1:${BUNDLE_DIR}>"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${BUNDLE_DIR}>")

if(WITH_UI)
    add_library(schallmauer_ui MODULE src/ui/ui_lv2.c src/ui/view.c)
    set_target_properties(schallmauer_ui PROPERTIES
        PREFIX ""
        SUFFIX ${LIB_SUFFIX}
        C_STANDARD 11
        C_VISIBILITY_PRESET hidden
        OBJC_VISIBILITY_PRESET hidden
        LIBRARY_OUTPUT_DIRECTORY "$<1:${BUNDLE_DIR}>"
        RUNTIME_OUTPUT_DIRECTORY "$<1:${BUNDLE_DIR}>")
    if(WIN32)
        target_link_libraries(schallmauer_ui PRIVATE user32 gdi32)
    elseif(APPLE)
        target_link_libraries(schallmauer_ui PRIVATE "-framework Cocoa")
    else()
        target_include_directories(schallmauer_ui PRIVATE ${X11_INCLUDE_DIR})
        target_link_libraries(schallmauer_ui PRIVATE ${X11_LIBRARIES} m)
    endif()
endif()

add_executable(test_dsp test/test_dsp.c)
set_target_properties(test_dsp PROPERTIES C_STANDARD 11)
target_link_libraries(test_dsp PRIVATE ${CMAKE_DL_LIBS})

add_executable(ui_render test/ui_render.c)
set_target_properties(ui_render PROPERTIES C_STANDARD 11)

add_executable(golden test/golden.c)
set_target_properties(golden PROPERTIES C_STANDARD 11)

add_executable(params_check test/params_check.c)
set_target_properties(params_check PROPERTIES C_STANDARD 11)

if(NOT MSVC)
    foreach(T schallmauer test_dsp ui_render params_check golden)
        target_compile_options(${T} PRIVATE -Wall -Wextra)
    endforeach()
    target_link_libraries(schallmauer PRIVATE m)
    foreach(T test_dsp ui_render params_check golden)
        target_link_libraries(${T} PRIVATE m)
        # The tests use parts of the panel and parameter headers, so the rest goes unused there.
        target_compile_options(${T} PRIVATE -Wno-unused-function)
    endforeach()
endif()

# The manifest names the Windows libraries and UI type, so other platforms get theirs.
file(READ bundle/manifest.ttl MANIFEST)
string(REPLACE "schallmauer.dll" "schallmauer${LIB_SUFFIX}" MANIFEST "${MANIFEST}")
string(REPLACE "schallmauer_ui.dll" "schallmauer_ui${LIB_SUFFIX}" MANIFEST "${MANIFEST}")
if(WITH_UI)
    string(REPLACE "ui:WindowsUI" "${UI_CLASS}" MANIFEST "${MANIFEST}")
else()
    string(REGEX REPLACE "# UI-BEGIN.*# UI-END\n?" "" MANIFEST "${MANIFEST}")
endif()
file(WRITE ${BUNDLE_DIR}/manifest.ttl "${MANIFEST}")
configure_file(bundle/schallmauer.ttl ${BUNDLE_DIR}/schallmauer.ttl COPYONLY)
# Licence, notices and the AI usage statement travel with every bundle.
set(SH_DOCS ${CMAKE_SOURCE_DIR}/LICENSE ${CMAKE_SOURCE_DIR}/NOTICE.md ${CMAKE_SOURCE_DIR}/AI_USAGE.md)
foreach(DOC ${SH_DOCS})
    configure_file(${DOC} ${BUNDLE_DIR}/ COPYONLY)
endforeach()
message(STATUS "Davidian Schallmauer: GPL-3.0-or-later, made with generative AI (Claude Code); see AI_USAGE.md")

enable_testing()
add_test(NAME test_dsp COMMAND test_dsp ${BUNDLE_DIR})
add_test(NAME ui_render COMMAND ui_render ${CMAKE_BINARY_DIR}/panel.bmp)
add_test(NAME params_check COMMAND params_check ${CMAKE_SOURCE_DIR}/bundle/schallmauer.ttl)

# "cmake --install build-cmake --prefix ~/.lv2" style installs: the bundle goes in <prefix>/lib/lv2
# by default, or straight into the prefix with -DLV2_INSTALL_DIR=.
set(LV2_INSTALL_DIR lib/lv2 CACHE STRING "Folder under the install prefix that receives the bundle")
install(DIRECTORY ${BUNDLE_DIR} DESTINATION ${LV2_INSTALL_DIR})

include(FetchContent)

# ---- VST3: build-cmake/VST3/<config>/Davidian Schallmauer.vst3 ----
# In its own folder, because the VST3 SDK sets compiler options (such as /fp:fast) for the whole
# folder that sets it up, and the LV2 build must not get them.
if(SCHALLMAUER_VST3)
    add_subdirectory(src/vst3)
endif()

# ---- Audio Unit: build-cmake/AU/Davidian Schallmauer.component ----
if(SCHALLMAUER_AU)
    if(NOT APPLE)
        message(FATAL_ERROR "Audio Units are macOS only")
    endif()
    enable_language(CXX)
    FetchContent_Declare(audiounitsdk
        GIT_REPOSITORY https://github.com/apple/AudioUnitSDK.git
        GIT_TAG AudioUnitSDK-1.2.0
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(audiounitsdk) # sources only; it has no CMake build
    set(AUSDK ${audiounitsdk_SOURCE_DIR})
    set(AUSDK_SOURCES)
    foreach(S AUBase AUBuffer AUBufferAllocator AUEffectBase AUInputElement AUOutputElement
              AUPlugInDispatch AUScopeElement ComponentBase)
        list(APPEND AUSDK_SOURCES ${AUSDK}/src/AudioUnitSDK/${S}.cpp)
    endforeach()

    # Names and four-character codes. Changing them breaks saved sessions.
    set(SH_AU_NAME "Davidian Schallmauer")
    set(SH_AU_BUNDLE_ID "com.davidian.schallmauer.au")
    set(SH_AU_MANUFACTURER "Dvdn")
    set(SH_AU_SUBTYPE "Shmr")
    set(SH_AU_FACTORY "DavidianSchallmauerAUFactory") # from AUSDK_COMPONENT_ENTRY in au_plugin.cpp
    math(EXPR SH_AU_VERSION_INT "${PROJECT_VERSION_MAJOR} * 65536 + ${PROJECT_VERSION_MINOR} * 256 + ${PROJECT_VERSION_PATCH}")
    configure_file(src/au/Info.plist.in ${CMAKE_BINARY_DIR}/au_plist/Info.plist @ONLY)

    add_library(schallmauer_au MODULE src/au/au_plugin.cpp src/au/au_view.m src/engine.c src/amp.c src/ui/view.c
        ${AUSDK_SOURCES})
    set_target_properties(schallmauer_au PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION component
        OUTPUT_NAME "${SH_AU_NAME}"
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_BINARY_DIR}/au_plist/Info.plist
        C_STANDARD 11
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        OBJC_VISIBILITY_PRESET hidden
        LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/AU>")
    target_include_directories(schallmauer_au PRIVATE ${AUSDK}/include)
    target_compile_definitions(schallmauer_au PRIVATE SH_AU_BUNDLE_ID="${SH_AU_BUNDLE_ID}"
        SH_AU_VIEW_FACTORY=DavidianSchallmauerAUViewFactory SH_AU_CONTAINER=DavidianSchallmauerAUContainer
        SH_VIEW_CLASS=DavidianSchallmauerAUPanel)
    target_compile_options(schallmauer_au PRIVATE $<$<COMPILE_LANGUAGE:C,OBJC>:-Wall -Wextra -Wno-unused-function>)
    target_link_libraries(schallmauer_au PRIVATE "-framework AudioToolbox" "-framework AudioUnit"
        "-framework CoreAudio" "-framework CoreFoundation" "-framework Cocoa")
    add_custom_command(TARGET schallmauer_au POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:schallmauer_au>/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy ${SH_DOCS} ${CMAKE_SOURCE_DIR}/licenses/Apache-2.0.txt
            "$<TARGET_BUNDLE_CONTENT_DIR:schallmauer_au>/Resources/"
        # An ad hoc signature: Apple silicon will not load unsigned code.
        COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:schallmauer_au>"
        VERBATIM)
endif()

# A host that loads the Audio Unit from its bundle, compares its sound with the LV2 build and
# opens its view. (auval, run in CI, checks the rest.)
if(SCHALLMAUER_AU)
    add_executable(au_check test/au_check.m)
    set_target_properties(au_check PROPERTIES C_STANDARD 11)
    target_compile_options(au_check PRIVATE -Wall -Wextra -Wno-unused-function)
    target_link_libraries(au_check PRIVATE "-framework AudioToolbox" "-framework AudioUnit"
        "-framework CoreFoundation" "-framework Cocoa")
    add_dependencies(au_check schallmauer_au schallmauer)
    add_test(NAME au_check COMMAND au_check "$<TARGET_BUNDLE_DIR:schallmauer_au>" "${BUNDLE_DIR}")
endif()
