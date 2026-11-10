cmake_minimum_required(VERSION 3.16)
project(novelreader_espeak_data VERSION 1.52.0 LANGUAGES C)
include(../EspeakDependencies.cmake)
add_executable(espeak-ng-bin ../../third_party/espeak-ng/src/espeak-ng.c)
target_link_libraries(espeak-ng-bin PRIVATE espeak-ng espeak-ng-config)
add_custom_target(speech-data
    COMMAND ${CMAKE_COMMAND} "-DTOOL=$<TARGET_FILE:espeak-ng-bin>"
        "-DSOURCE=${ESPEAK_SOURCE}" "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/regenerate.cmake"
    DEPENDS espeak-ng-bin VERBATIM)
