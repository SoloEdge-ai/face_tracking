cmake_minimum_required(VERSION 3.25)
project(face_tracking VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

option(FACE_TRACKING_BUILD_ZENOH "Build the Zenoh transport adapter" ON)
option(FACE_TRACKING_BUILD_TESTS "Build tests" ON)
option(FACE_TRACKING_USE_SYSTEM_OPENCV "Use a compatible system OpenCV instead of the pinned source build" OFF)

include(FetchContent)

if(FACE_TRACKING_USE_SYSTEM_OPENCV)
  find_package(Eigen3 QUIET)
  find_package(OpenEXR QUIET)
  find_package(OpenCV 4.9 REQUIRED COMPONENTS core imgcodecs imgproc videoio dnn)
else()
  set(BUILD_LIST core,imgproc,imgcodecs,videoio,dnn CACHE STRING "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(BUILD_PERF_TESTS OFF CACHE BOOL "" FORCE)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_opencv_apps OFF CACHE BOOL "" FORCE)
  set(BUILD_opencv_python_bindings_generator OFF CACHE BOOL "" FORCE)
  set(BUILD_PROTOBUF OFF CACHE BOOL "" FORCE)
  set(PROTOBUF_UPDATE_FILES ON CACHE BOOL "" FORCE)
  set(BUILD_JAVA OFF CACHE BOOL "" FORCE)
  set(WITH_GTK OFF CACHE BOOL "" FORCE)
  set(WITH_QT OFF CACHE BOOL "" FORCE)
  set(WITH_OPENCL OFF CACHE BOOL "" FORCE)
  set(WITH_IPP OFF CACHE BOOL "" FORCE)
  set(WITH_ITT OFF CACHE BOOL "" FORCE)
  set(WITH_ADE OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    opencv
    URL https://codeload.github.com/opencv/opencv/tar.gz/refs/tags/4.12.0
    URL_HASH SHA256=44c106d5bb47efec04e531fd93008b3fcd1d27138985c5baf4eafac0e1ec9e9d
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
  )
  FetchContent_MakeAvailable(opencv)
  set(OpenCV_LIBS opencv_core opencv_imgproc opencv_imgcodecs opencv_videoio opencv_dnn)
  set(OpenCV_INCLUDE_DIRS ${opencv_SOURCE_DIR}/modules/core/include ${opencv_SOURCE_DIR}/modules/imgproc/include ${opencv_SOURCE_DIR}/modules/imgcodecs/include ${opencv_SOURCE_DIR}/modules/videoio/include ${opencv_SOURCE_DIR}/modules/dnn/include ${opencv_BINARY_DIR} ${CMAKE_BINARY_DIR})
endif()
find_package(Protobuf 3.21 REQUIRED)
find_package(yaml-cpp 0.7 REQUIRED)
find_package(Threads REQUIRED)

if(FACE_TRACKING_BUILD_TESTS)
  enable_testing()
  find_package(GTest REQUIRED)
endif()

add_subdirectory(modules/face_tracking_schemas)
add_subdirectory(modules/camera_service)
add_subdirectory(modules/face_detector_tracker)
add_subdirectory(modules/pixel_center_controller)
add_subdirectory(modules/servo_driver)

if(FACE_TRACKING_BUILD_ZENOH)
  set(ZENOHCXX_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
  set(ZENOHCXX_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(ZENOHCXX_EXAMPLES_PROTOBUF OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    zenohcxx
    URL https://codeload.github.com/eclipse-zenoh/zenoh-cpp/tar.gz/refs/tags/1.10.0
    URL_HASH SHA256=c7aed678b421dcce0e3c39fa9ce5dc9bdd92180000beca1e0db9e0c6e78a4f3d
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
  )
  FetchContent_MakeAvailable(zenohcxx)
  add_subdirectory(adapters/zenoh)
endif()

add_subdirectory(modules/pan_tilt_bringup)
