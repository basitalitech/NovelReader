cmake_minimum_required(VERSION 3.16)

option(NOVELREADER_HOST_TESTS "Build portable reader tests instead of the Vita app" OFF)
if(NOT NOVELREADER_HOST_TESTS AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(NOT DEFINED ENV{VITASDK})
        message(FATAL_ERROR "Set VITASDK or use -DNOVELREADER_HOST_TESTS=ON")
    endif()
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Vita toolchain")
endif()

project(NovelReader VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(reader_core src/reader.cpp)
target_include_directories(reader_core PUBLIC include)
target_compile_options(reader_core PRIVATE -Wall -Wextra -Wpedantic)

if(NOVELREADER_HOST_TESTS)
    enable_testing()
    add_executable(reader_tests tests/reader_tests.cpp)
    target_link_libraries(reader_tests PRIVATE reader_core)
    target_compile_options(reader_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME reader_core COMMAND reader_tests)
else()
    include("${VITASDK}/share/vita.cmake" REQUIRED)
    add_executable(novelreader src/main.cpp)
    target_compile_options(novelreader PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(novelreader PRIVATE reader_core vita2d
        SceDisplay_stub SceGxm_stub SceSysmodule_stub SceCtrl_stub
        ScePgf_stub ScePvf_stub SceCommonDialog_stub SceAppMgr_stub
        SceIofilemgr_stub ScePower_stub SceProcessmgr_stub
        freetype png jpeg z m)
    vita_create_self(eboot.bin novelreader UNSAFE)
    vita_create_vpk(NovelReader.vpk NVRD00001 eboot.bin
        VERSION "00.10" NAME "Novel Reader"
        FILE assets/welcome.txt welcome.txt)
endif()
