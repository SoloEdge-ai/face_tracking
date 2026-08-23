add_library(camera_service src/camera_service.cpp)
add_library(face_tracking::camera ALIAS camera_service)
target_include_directories(camera_service
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(camera_service PUBLIC face_tracking::schemas PRIVATE ${OpenCV_LIBS} Threads::Threads)
target_compile_options(camera_service PRIVATE -Wall -Wextra -Wpedantic)

if(FACE_TRACKING_BUILD_TESTS)
  add_executable(camera_service_tests tests/camera_service_test.cpp)
  target_include_directories(camera_service_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(camera_service_tests PRIVATE face_tracking::camera ${OpenCV_LIBS} GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(camera_service_tests)
endif()
