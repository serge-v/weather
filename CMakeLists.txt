# Serge Voilokov, 2015
# cmake file for building weather tool.
#
#
# Generate makefile:
# ./gen-cmake.sh

cmake_minimum_required(VERSION 2.6)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(CURL REQUIRED)
find_package(LibXml2 REQUIRED)

include(../common/macros.cmake)

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${LIBXML2_INCLUDE_DIR})
include_directories(${CURL_INCLUDE_DIR})
include_directories(..)

gen_version_c()

add_subdirectory(../common "${CMAKE_BINARY_DIR}/common")

add_executable(
	weather
	parse-dwml.c
	${CMAKE_CURRENT_BINARY_DIR}/version.c
)

target_link_libraries(
	weather
	${LIBXML2_LIBRARIES}
	${CURL_LIBRARY}
	svc
)
