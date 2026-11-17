# SPDX-License-Identifier: GPL-3.0-or-later
file(GLOB CTRM_CORE_TESTS CONFIGURE_DEPENDS tst_*.cpp)
foreach(test_source IN LISTS CTRM_CORE_TESTS)
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} PRIVATE ctrm_core ctrm_test_support Qt6::Test)
    ctrm_set_warnings(${test_name})
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# Renames 1,000 files slowly, so that tst_safety can kill it in the middle of a batch.
add_executable(ctrm_crash_helper helpers/crashhelper.cpp)
target_link_libraries(ctrm_crash_helper PRIVATE ctrm_core)
ctrm_set_warnings(ctrm_crash_helper)
add_dependencies(tst_safety ctrm_crash_helper)
target_compile_definitions(tst_safety PRIVATE
    CTRM_CRASH_HELPER="$<TARGET_FILE:ctrm_crash_helper>")
