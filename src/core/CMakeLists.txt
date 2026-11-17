# SPDX-License-Identifier: GPL-3.0-or-later
configure_file(version.h.in ${CMAKE_CURRENT_BINARY_DIR}/version.h @ONLY)

file(GLOB_RECURSE CTRM_CORE_SOURCES CONFIGURE_DEPENDS *.cpp *.h)
list(FILTER CTRM_CORE_SOURCES EXCLUDE REGEX "_(posix|macos|linux|win)\\.(cpp|h)$")
if(APPLE)
    file(GLOB CTRM_PLATFORM CONFIGURE_DEPENDS fs/*_posix.cpp fs/*_macos.cpp)
elseif(UNIX)
    file(GLOB CTRM_PLATFORM CONFIGURE_DEPENDS fs/*_posix.cpp fs/*_linux.cpp)
elseif(WIN32)
    file(GLOB CTRM_PLATFORM CONFIGURE_DEPENDS fs/*_win.cpp)
endif()

# MSVC lib.exe writes no .lib for a library with no object files. While core has no .cpp
# file, compile a generated empty one.
set(CTRM_CORE_CPP ${CTRM_CORE_SOURCES} ${CTRM_PLATFORM})
list(FILTER CTRM_CORE_CPP INCLUDE REGEX "\\.cpp$")
if(NOT CTRM_CORE_CPP)
    set(CTRM_CORE_STUB ${CMAKE_CURRENT_BINARY_DIR}/ctrm_core_stub.cpp)
    file(CONFIGURE OUTPUT ${CTRM_CORE_STUB}
        CONTENT "// Generated: ctrm_core has no source file yet.\n#include \"version.h\"\n")
    list(APPEND CTRM_CORE_SOURCES ${CTRM_CORE_STUB})
endif()

add_library(ctrm_core STATIC
    ${CMAKE_CURRENT_BINARY_DIR}/version.h
    ${CTRM_CORE_SOURCES}
    ${CTRM_PLATFORM}
)
target_include_directories(ctrm_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(ctrm_core PUBLIC Qt6::Core)

qt_add_resources(ctrm_core "builtin_presets"
    PREFIX "/ctrm/presets"
    BASE presets/builtin
    FILES
        presets/builtin/lower-case-extensions.ctrm.json
        presets/builtin/photo-numbering.ctrm.json
        presets/builtin/remove-tags.ctrm.json
        presets/builtin/spaces-to-underscores.ctrm.json
)
ctrm_set_warnings(ctrm_core)
ctrm_autogen_track_sources(ctrm_core)
