# Serge Voilokov, 2015
# cmake file for building weather tool.
#
#
# Usage:
# cd ~/b/weatherb
# cmake -DCMAKE_TOOLCHAIN_FILE=~/src/xtree/ctests/weather/macports.cmake ~/src/xtree/ctests/weather/

cmake_minimum_required(VERSION 2.6)

find_package(CURL REQUIRED)
find_package(LibXml2 REQUIRED)

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${LIBXML2_INCLUDE_DIR})
include_directories(${CURL_INCLUDE_DIR})

add_executable(
	weather
	parse-dwml.c
	../common/net.c ../common/net.h
	../common/xml.c ../common/xml.h
	../common/struct.c ../common/struct.h
	version.c version.h
)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/version.c" "${CMAKE_CURRENT_SOURCE_DIR}/version.h"
	COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/../common/genversion.sh
	DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/../common/genversion.sh CMakeLists.txt
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	COMMENT "Generating version."
)

target_link_libraries(weather ${LIBXML2_LIBRARIES} ${CURL_LIBRARY})
