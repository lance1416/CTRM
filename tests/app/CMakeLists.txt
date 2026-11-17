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

# The alignment tests also run with the native style: it moves widgets inside their layout items,
# so an edge that is correct in Fusion can be wrong there. Platforms with no native style (Linux)
# have only the Fusion run.
if(APPLE)
    set(CTRM_NATIVE_STYLE macOS)
elseif(WIN32)
    set(CTRM_NATIVE_STYLE windows11)
endif()
if(CTRM_NATIVE_STYLE)
    foreach(test_name tst_app_forms tst_app_editors1 tst_app_editors2 tst_app_rulepanel)
        add_test(NAME ${test_name}_native COMMAND ${test_name} -style ${CTRM_NATIVE_STYLE})
        set_tests_properties(${test_name}_native PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            FAIL_REGULAR_EXPRESSION "invalid style override")
    endforeach()
endif()
