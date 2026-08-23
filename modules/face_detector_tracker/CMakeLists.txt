add_library(face_detector_tracker src/detector_service.cpp src/opencv_yolo.cpp)
add_library(face_tracking::detector ALIAS face_detector_tracker)
target_include_directories(face_detector_tracker
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(face_detector_tracker PUBLIC face_tracking::schemas PRIVATE ${OpenCV_LIBS} Threads::Threads)
target_compile_options(face_detector_tracker PRIVATE -Wall -Wextra -Wpedantic)

if(FACE_TRACKING_BUILD_TESTS)
  add_executable(face_detector_tracker_tests tests/detector_service_test.cpp)
  target_include_directories(face_detector_tracker_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(face_detector_tracker_tests PRIVATE face_tracking::detector ${OpenCV_LIBS} GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(face_detector_tracker_tests)

  add_executable(face_tracking_detector_benchmark tests/detector_benchmark.cpp)
  target_include_directories(face_tracking_detector_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(face_tracking_detector_benchmark PRIVATE face_tracking::detector ${OpenCV_LIBS})
endif()
