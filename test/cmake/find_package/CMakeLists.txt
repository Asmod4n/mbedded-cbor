cmake_minimum_required(VERSION 3.25)
project(consumer LANGUAGES CXX)

find_package(mbedded-cbor ${CBOR_VERSION} CONFIG REQUIRED)
add_executable(consumer ../consumer.cpp)
target_link_libraries(consumer PRIVATE mbedded-cbor::mbedded-cbor)
separate_arguments(warning_flags UNIX_COMMAND "${CBOR_WARNING_FLAGS}")
target_compile_options(consumer PRIVATE ${warning_flags})
