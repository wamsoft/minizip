set(TPSTUB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../tp_stub CACHE PATH "Path to tp_stub directory")
include(${TPSTUB_DIR}/krkrz.cmake)

find_package(minizip-ng CONFIG REQUIRED)

krkrz_plugin(minizip
    NCBIND
    SOURCES
        ioapi.cpp
        main.cpp
        LicensesGen.cpp
        storage.cpp
    LIBRARIES
        MINIZIP::minizip-ng
)
