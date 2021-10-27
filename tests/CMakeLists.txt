cmake_minimum_required(VERSION 3.12)

project(PromiseTests)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)

file(GLOB_RECURSE INTEGRATION_TEST_SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/Integration/Simple.cpp")
add_executable(IntegrationTest ${INTEGRATION_TEST_SOURCE_FILES})

file(GLOB_RECURSE INTEGRATION_TEST_SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/Integration/HTTP.cpp")
add_executable(HTTPTest ${INTEGRATION_TEST_SOURCE_FILES})
target_link_libraries(HTTPTest ${CONAN_LIBS})

file(GLOB_RECURSE UNITARY_TEST_SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/Unitary/*.cpp")
add_executable(UnitaryTest ${UNITARY_TEST_SOURCE_FILES})
target_include_directories(UnitaryTest PRIVATE ${CONAN_INCLUDE_DIRS_CATCH2})
