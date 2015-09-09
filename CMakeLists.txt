# Serge Voilokov, 2015
# cmake file for building weather tool.
#
#
# Generate makefile:
# cd ~/b/weatherb
# cmake -DCMAKE_TOOLCHAIN_FILE=~/src/xtree/weather/macports.cmake ~/src/xtree/weather/
#
# Generate Xcode project:
# cd ~/b/weatherx
# cmake -G Xcode -DCMAKE_TOOLCHAIN_FILE=~/src/xtree/weather/macports.cmake ~/src/xtree/weather/

cmake_minimum_required(VERSION 2.6)

find_package(CURL REQUIRED)
find_package(LibXml2 REQUIRED)

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${LIBXML2_INCLUDE_DIR})
include_directories(${CURL_INCLUDE_DIR})

add_executable(
	weather
	parse-dwml.c
	common/net.c common/net.h
	common/xml.c common/xml.h
	common/struct.c common/struct.h
	version.c version.h
)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/version.c" "${CMAKE_CURRENT_BINARY_DIR}/version.h"
	COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/common/genversion.sh
	ARGS ${CMAKE_CURRENT_BINARY_DIR}
	DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/common/genversion.sh CMakeLists.txt
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	COMMENT "Generating version."
)

target_link_libraries(weather ${LIBXML2_LIBRARIES} ${CURL_LIBRARY})
