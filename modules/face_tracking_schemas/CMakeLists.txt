set(PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/proto/face_tracking_v2.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILE})
set(PY_PROTO_DIR ${PROJECT_SOURCE_DIR}/modules/web_hmi_target_manager/service/src/face_tracking_hmi/generated)
set(PY_PROTO_FILE ${PY_PROTO_DIR}/face_tracking_v2_pb2.py)
add_custom_command(
  OUTPUT ${PY_PROTO_FILE}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${PY_PROTO_DIR}
  COMMAND $<TARGET_FILE:protobuf::protoc> --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto --python_out=${PY_PROTO_DIR} ${PROTO_FILE}
  DEPENDS ${PROTO_FILE} protobuf::protoc
  COMMENT "Generating Python Protobuf v2 binding"
  VERBATIM
)
add_custom_target(face_tracking_python_proto ALL DEPENDS ${PY_PROTO_FILE})

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
  add_dependencies(face_tracking_schemas_tests face_tracking_python_proto)
  target_link_libraries(face_tracking_schemas_tests PRIVATE face_tracking::schemas GTest::gtest_main)
  target_compile_definitions(face_tracking_schemas_tests PRIVATE FACE_TRACKING_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/integration/fixtures")
  include(GoogleTest)
  gtest_discover_tests(face_tracking_schemas_tests)
endif()
