set(TPSTUB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../tp_stub CACHE PATH "Path to tp_stub directory")
include(${TPSTUB_DIR}/krkrz.cmake)

add_link_options("$<$<CXX_COMPILER_ID:MSVC>:/SUBSYSTEM:WINDOWS>")

find_package(minizip-ng CONFIG REQUIRED)

# add_subdirectory(minizip)

krkrz_plugin(minizip
    NCBIND
    SOURCES
        ioapi.cpp
        main.cpp
        storage.cpp
    LIBRARIES
        MINIZIP::minizip-ng
        # libminizip
)

target_compile_definitions(${PROJECT_NAME} PUBLIC
	MZ_COMPAT_VERSION=100
)

target_include_directories(${PROJECT_NAME} PUBLIC
    minizip
)
