# SPDX-License-Identifier: GPL-3.0-or-later
file(GLOB CTRM_CORE_TESTS CONFIGURE_DEPENDS tst_*.cpp)
foreach(test_source IN LISTS CTRM_CORE_TESTS)
  get_filename_component(test_name ${test_source} NAME_WE)
  add_executable(${test_name} ${test_source})
  target_link_libraries(${test_name} PRIVATE ctrm_core ctrm_test_support Qt6::Test)
  ctrm_set_warnings(${test_name})
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
