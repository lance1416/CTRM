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
# Qt before 6.9.3, and 6.10.0, crash in QMacStyle on the offscreen platform: it uses the window id
# as an NSView when it paints a shown window. Those versions have no native run.
if(APPLE AND NOT (Qt6_VERSION VERSION_LESS 6.9.3 OR Qt6_VERSION VERSION_EQUAL 6.10.0))
    set(CTRM_NATIVE_STYLE macOS)
elseif(APPLE)
    message(STATUS "Qt ${Qt6_VERSION}: no native macOS style test runs (QMacStyle offscreen crash)")
elseif(WIN32)
    set(CTRM_NATIVE_STYLE windows11)
endif()
if(CTRM_NATIVE_STYLE)
    foreach(test_name tst_app_forms tst_app_editors1 tst_app_editors2 tst_app_rulepanel tst_app_style
            tst_app_keyboardmode)
        add_test(NAME ${test_name}_native COMMAND ${test_name} -style ${CTRM_NATIVE_STYLE})
        set_tests_properties(${test_name}_native PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            FAIL_REGULAR_EXPRESSION "invalid style override")
    endforeach()
endif()
# QMacStyle draws focus rings only with the Cocoa platform (offscreen it draws nothing). This test
# paints focus frames with no shown window, so it can run with Cocoa (O11 rule 3 on macOS).
if(CTRM_NATIVE_STYLE STREQUAL "macOS")
    add_test(NAME tst_app_keyboardmode_cocoa
        COMMAND tst_app_keyboardmode macFocusFrameRules -platform cocoa -style macOS)
    set_tests_properties(tst_app_keyboardmode_cocoa PROPERTIES
        FAIL_REGULAR_EXPRESSION "invalid style override;SKIP"
        TIMEOUT 60)
endif()
