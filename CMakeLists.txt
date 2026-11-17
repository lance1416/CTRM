# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.24)
# CMake 3.29+: link each static library once (Apple ld warns on duplicates).
if(POLICY CMP0156)
    cmake_policy(SET CMP0156 NEW)
endif()
project(CTRM VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets Svg Test)
qt_standard_project_setup()

function(ctrm_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# AUTOMOC runs again only when a file in its depfile changes: the CMake list files of the
# directory and the known sources. A file that file(GLOB CONFIGURE_DEPENDS) finds later
# changes the source list but no list file, so moc does not run for a new Q_OBJECT header.
# Fix: write the source list into a generated list file (rewritten only when the list
# changes) and include() it, so that the autogen depfile contains it.
# AUTOGEN_TARGET_DEPENDS does not work here: with Ninja it is only an order-only dependency.
function(ctrm_autogen_track_sources target)
    get_target_property(sources ${target} SOURCES)
    list(JOIN sources "\n# " content)
    set(list_file ${CMAKE_CURRENT_BINARY_DIR}/${target}_sources.cmake)
    file(CONFIGURE OUTPUT ${list_file} CONTENT "# Generated. Sources of ${target}:\n# ${content}\n")
    include(${list_file})
endfunction()

add_subdirectory(src/core)
add_subdirectory(src/app)

enable_testing()
add_subdirectory(tests)
