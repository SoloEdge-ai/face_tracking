add_library(pan_tilt_bringup_lib src/router_manager.cpp)
target_include_directories(pan_tilt_bringup_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(pan_tilt_bringup_lib PRIVATE -Wall -Wextra -Wpedantic)

add_executable(face_tracking_bringup src/main.cpp)
target_link_libraries(face_tracking_bringup PRIVATE pan_tilt_bringup_lib)
install(TARGETS face_tracking_bringup RUNTIME DESTINATION bin)

if(FACE_TRACKING_BUILD_TESTS)
  add_executable(pan_tilt_bringup_tests tests/router_manager_test.cpp)
  target_link_libraries(pan_tilt_bringup_tests PRIVATE pan_tilt_bringup_lib GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(pan_tilt_bringup_tests)
endif()
