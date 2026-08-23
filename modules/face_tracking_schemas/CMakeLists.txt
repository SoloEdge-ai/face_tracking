set(PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/proto/face_tracking_v2.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILE})

add_library(face_tracking_schemas
  src/codec.cpp
  src/settings.cpp
  src/types.cpp
  ${PROTO_SRCS}
  ${PROTO_HDRS}
)
add_library(face_tracking::schemas ALIAS face_tracking_schemas)
target_include_directories(face_tracking_schemas
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(face_tracking_schemas PUBLIC protobuf::libprotobuf yaml-cpp)
target_compile_options(face_tracking_schemas PRIVATE -Wall -Wextra -Wpedantic)

if(FACE_TRACKING_BUILD_TESTS)
  add_executable(face_tracking_schemas_tests tests/schemas_test.cpp)
  target_link_libraries(face_tracking_schemas_tests PRIVATE face_tracking::schemas GTest::gtest_main)
  target_compile_definitions(face_tracking_schemas_tests PRIVATE FACE_TRACKING_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/integration/fixtures")
  include(GoogleTest)
  gtest_discover_tests(face_tracking_schemas_tests)
endif()
