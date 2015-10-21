# Serge Voilokov, 2015
# cmake file for weather forecast UI.
#
#
# Usage:
# ./gen-cmake.sh
# make

cmake_minimum_required(VERSION 2.8)

find_library(mysql_LIBRARY NAMES mysqlclient PATHS ${CMAKE_FIND_ROOT_PATH}/lib/mysql55/mysql/)

find_path(mysql_INCLUDE mysql/mysql.h PATHS ${CMAKE_FIND_ROOT_PATH}/include/mysql55)

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${mysql_INCLUDE})
include_directories(${CMAKE_FIND_ROOT_PATH}/include)

include(common/macros.cmake)

gen_version_c()

set(
	resources
	main.html
	synopsis.txt
	usage.txt
)

generate_resources("${resources}" generated)

source_group("html" FILES main.html)

add_executable(
	weatherui
	main.c
	config.c config.h
	main.html.c main.html.h
	synopsis.txt.c
	usage.txt.c
	common/struct.c
	common/regexp.c
	common/mysql.c
	version.c version.h
	${resources}
	${generated}
)

target_link_libraries(weatherui ${json_LIBRARY} ${mysql_LIBRARY})