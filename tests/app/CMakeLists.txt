# SPDX-License-Identifier: GPL-3.0-or-later
file(GLOB CTRM_APP_TESTS CONFIGURE_DEPENDS tst_app_*.cpp)
foreach(test_source IN LISTS CTRM_APP_TESTS)
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} PRIVATE ctrm_app ctrm_test_support Qt6::Test)
    ctrm_set_warnings(${test_name})
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

# Timing tests (QSKIP in Debug). The Release perf job runs only this label.
set_tests_properties(tst_app_preview tst_app_acceptance PROPERTIES LABELS perf)
