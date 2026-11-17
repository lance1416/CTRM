# SPDX-License-Identifier: GPL-3.0-or-later
file(GLOB CTRM_TEST_SUPPORT_SOURCES CONFIGURE_DEPENDS *.cpp *.h)
add_library(ctrm_test_support STATIC ${CTRM_TEST_SUPPORT_SOURCES})
target_include_directories(ctrm_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ctrm_test_support PUBLIC ctrm_core Qt6::Core Qt6::Test)
ctrm_set_warnings(ctrm_test_support)
ctrm_autogen_track_sources(ctrm_test_support)
