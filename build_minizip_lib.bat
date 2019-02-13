cd minizip
cmake . -DCMAKE_USER_MAKE_RULES_OVERRIDE=../vs_flag_override.cmake
cmake --build . --config Release
cmake --build . --config Debug
