add_library(servo_driver src/servo_driver.cpp src/servo_sweep.cpp)
add_library(face_tracking::servo ALIAS servo_driver)
target_include_directories(servo_driver PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(servo_driver PUBLIC face_tracking::schemas Threads::Threads)
target_compile_options(servo_driver PRIVATE -Wall -Wextra -Wpedantic)

add_library(servo_hardware_pwm src/hardware_pwm.cpp)
add_library(face_tracking::servo_hardware_pwm ALIAS servo_hardware_pwm)
target_include_directories(servo_hardware_pwm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(servo_hardware_pwm PUBLIC face_tracking::servo)
target_compile_options(servo_hardware_pwm PRIVATE -Wall -Wextra -Wpedantic)

add_executable(face_tracking_servo_sweep_test src/servo_sweep_main.cpp)
target_link_libraries(face_tracking_servo_sweep_test PRIVATE
  face_tracking::servo
  face_tracking::servo_hardware_pwm
)
target_compile_options(face_tracking_servo_sweep_test PRIVATE -Wall -Wextra -Wpedantic)

if(FACE_TRACKING_BUILD_TESTS)
  add_executable(servo_driver_tests tests/servo_driver_test.cpp)
  target_link_libraries(servo_driver_tests PRIVATE
    face_tracking::servo
    face_tracking::servo_hardware_pwm
    GTest::gtest_main
  )
  include(GoogleTest)
  gtest_discover_tests(servo_driver_tests)
endif()
