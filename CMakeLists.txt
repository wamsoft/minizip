cmake_minimum_required(VERSION 3.16)

# UTF-8 source
add_compile_options("$<$<AND:$<C_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:C>>:/utf-8>")
add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/utf-8>")
add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/Zc:__cplusplus>")

add_link_options("$<$<CXX_COMPILER_ID:MSVC>:/SUBSYSTEM:WINDOWS>")

set(PROJECT_NAME minizip)
set(PROJECT_VERSION 1.00)

project(${PROJECT_NAME} VERSION ${PROJECT_VERSION})

find_package(minizip-ng CONFIG REQUIRED)

if(NOT TARGET ncbind)
add_subdirectory(../ncbind ${CMAKE_CURRENT_BINARY_DIR}/ncbind)
endif()

# add_subdirectory(minizip)

add_library(${PROJECT_NAME} SHARED
    ioapi.cpp
    main.cpp
    storage.cpp
)

target_compile_definitions(${PROJECT_NAME} PUBLIC
	MZ_COMPAT_VERSION=100
)

target_include_directories(${PROJECT_NAME} PUBLIC
    minizip
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    ncbind
	MINIZIP::minizip-ng
    # libminizip
)
