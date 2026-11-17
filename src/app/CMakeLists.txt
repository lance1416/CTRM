# SPDX-License-Identifier: GPL-3.0-or-later
# All UI code is in the static library ctrm_app, so tests can use it. main.cpp is the only
# file of the ctrm executable.
file(GLOB_RECURSE CTRM_APP_SOURCES CONFIGURE_DEPENDS *.cpp *.h)
list(REMOVE_ITEM CTRM_APP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

# icons.cpp calls Q_INIT_RESOURCE(icons), so the linker keeps the resource of the static library.
qt_add_resources(CTRM_APP_RESOURCES resources/icons.qrc)

add_library(ctrm_app STATIC ${CTRM_APP_SOURCES} ${CTRM_APP_RESOURCES})
target_include_directories(ctrm_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ctrm_app PUBLIC ctrm_core Qt6::Widgets PRIVATE Qt6::Svg)
ctrm_set_warnings(ctrm_app)

qt_add_executable(ctrm main.cpp)
set_target_properties(ctrm PROPERTIES
    MACOSX_BUNDLE ON
    WIN32_EXECUTABLE ON
)
target_link_libraries(ctrm PRIVATE ctrm_app)
ctrm_set_warnings(ctrm)
