# SPDX-License-Identifier: GPL-3.0-or-later
qt_add_executable(ctrm main.cpp)
set_target_properties(ctrm PROPERTIES
    MACOSX_BUNDLE ON
    WIN32_EXECUTABLE ON
)
target_link_libraries(ctrm PRIVATE ctrm_core Qt6::Widgets)
ctrm_set_warnings(ctrm)
