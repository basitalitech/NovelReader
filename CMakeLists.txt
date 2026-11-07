cmake_minimum_required(VERSION 3.16)

option(NOVELREADER_HOST_TESTS "Build portable reader tests instead of the Vita app" OFF)
if(NOT NOVELREADER_HOST_TESTS AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(NOT DEFINED ENV{VITASDK})
        message(FATAL_ERROR "Set VITASDK or use -DNOVELREADER_HOST_TESTS=ON")
    endif()
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Vita toolchain")
endif()

project(NovelReader VERSION 0.7.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(cmake/OnlineDependencies.cmake)

add_library(epub_dependencies STATIC third_party/tinyxml2/tinyxml2.cpp
    third_party/minizip/unzip.c third_party/minizip/ioapi.c)
target_include_directories(epub_dependencies PUBLIC third_party/tinyxml2 third_party/minizip)
target_compile_definitions(epub_dependencies PRIVATE USE_FILE32API NOUNCRYPT)
if(NOVELREADER_HOST_TESTS)
    find_package(ZLIB REQUIRED)
    target_link_libraries(epub_dependencies PUBLIC ZLIB::ZLIB)
else()
    target_link_libraries(epub_dependencies PUBLIC z)
endif()

add_library(reader_core src/reader.cpp src/epub.cpp src/library.cpp src/image_info.cpp src/layout.cpp src/controls.cpp src/online_store.cpp)
target_link_libraries(reader_core PRIVATE epub_dependencies mbedcrypto)
target_include_directories(reader_core PUBLIC include)
target_compile_options(reader_core PRIVATE -Wall -Wextra -Wpedantic)
add_library(reader_online src/online_html.cpp src/online_adapter.cpp src/online_service.cpp)
target_include_directories(reader_online PUBLIC include)
target_link_libraries(reader_online PUBLIC reader_core PRIVATE novel_lua CURL::libcurl)
target_compile_options(reader_online PRIVATE -Wall -Wextra -Wpedantic)
if(NOVELREADER_HOST_TESTS)
    find_package(Threads REQUIRED)
    target_link_libraries(reader_online PRIVATE Threads::Threads)
else()
    # libstdc++ uses weak pthread probes (including pthread_cancel and
    # pthread_once). A normal static -lpthread leaves these unresolved and
    # std::thread reports that threading is unavailable at runtime.
    target_link_libraries(reader_online PRIVATE -Wl,--whole-archive pthread -Wl,--no-whole-archive
        SceNet_stub SceNetCtl_stub SceSysmodule_stub)
endif()

if(NOVELREADER_HOST_TESTS)
    enable_testing()
    add_executable(online_tests tests/online_tests.cpp)
    target_link_libraries(online_tests PRIVATE reader_online)
    target_compile_definitions(online_tests PRIVATE NOVELREADER_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets")
    target_compile_options(online_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME online_sources COMMAND online_tests)
    add_executable(reader_tests tests/reader_tests.cpp)
    target_link_libraries(reader_tests PRIVATE reader_core)
    target_compile_options(reader_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME reader_core COMMAND reader_tests)
    add_executable(epub_tests tests/epub_tests.cpp)
    target_link_libraries(epub_tests PRIVATE reader_core epub_dependencies)
    target_compile_options(epub_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME epub_reader COMMAND epub_tests)
    add_executable(library_tests tests/library_tests.cpp)
    target_link_libraries(library_tests PRIVATE reader_core)
    target_compile_options(library_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME library_and_images COMMAND library_tests)
    add_executable(layout_tests tests/layout_tests.cpp)
    target_link_libraries(layout_tests PRIVATE reader_core)
    target_compile_options(layout_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME inline_layout COMMAND layout_tests)
    add_executable(controls_tests tests/controls_tests.cpp)
    target_link_libraries(controls_tests PRIVATE reader_core)
    target_compile_options(controls_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME analog_and_touch COMMAND controls_tests)
else()
    include("${VITASDK}/share/vita.cmake" REQUIRED)
    add_executable(novelreader src/main.cpp src/vita_images.cpp)
    # Leave space for vita-elf-create's import metadata between load segments.
    target_link_options(novelreader PRIVATE -Wl,--defsym=__sce_headroom=0x10000)
    target_compile_options(novelreader PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(novelreader PRIVATE reader_online vita2d
        SceDisplay_stub SceGxm_stub SceSysmodule_stub SceCtrl_stub SceTouch_stub
        ScePgf_stub ScePvf_stub SceCommonDialog_stub SceAppMgr_stub
        SceIofilemgr_stub ScePower_stub SceProcessmgr_stub SceIme_stub SceAppUtil_stub
        freetype png turbojpeg jpeg z m)
    add_custom_command(TARGET novelreader POST_BUILD
        COMMAND ${CMAKE_COMMAND} "-DNM=${CMAKE_NM}" "-DELF=$<TARGET_FILE:novelreader>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckVitaThreads.cmake"
        COMMENT "Checking Vita pthread runtime support" VERBATIM)
    vita_create_self(eboot.bin novelreader UNSAFE)
    vita_create_vpk(NovelReader.vpk NVRD00001 eboot.bin
        VERSION "00.70" NAME "Novel Reader"
        FILE assets/welcome.txt welcome.txt
        FILE assets/cacert.pem cacert.pem
        FILE assets/sources/common.lua sources/common.lua
        FILE assets/sources/json.lua sources/json.lua
        FILE assets/sources/novgo.lua sources/novgo.lua
        FILE assets/sources/asura.lua sources/asura.lua
        FILE assets/sources/beast.lua sources/beast.lua)
endif()
