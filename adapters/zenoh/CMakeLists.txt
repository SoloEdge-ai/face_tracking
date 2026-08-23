add_library(face_tracking_zenoh_adapter src/zenoh_adapter.cpp)
add_library(face_tracking::zenoh ALIAS face_tracking_zenoh_adapter)
target_include_directories(face_tracking_zenoh_adapter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(face_tracking_zenoh_adapter PUBLIC face_tracking::schemas face_tracking::camera face_tracking::detector zenohcxx::zenohc)
target_compile_options(face_tracking_zenoh_adapter PRIVATE -Wall -Wextra -Wpedantic)

add_executable(face_tracking_camera_zenoh apps/camera_main.cpp)
target_link_libraries(face_tracking_camera_zenoh PRIVATE face_tracking::camera face_tracking::zenoh)

add_executable(face_tracking_detector_zenoh apps/detector_main.cpp)
target_link_libraries(face_tracking_detector_zenoh PRIVATE face_tracking::detector face_tracking::zenoh)

install(TARGETS face_tracking_camera_zenoh face_tracking_detector_zenoh RUNTIME DESTINATION bin)
