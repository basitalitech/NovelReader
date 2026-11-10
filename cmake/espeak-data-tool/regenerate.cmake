# Native-only data compilation. Normal app builds consume checked-in data.
file(MAKE_DIRECTORY "${OUTPUT}/espeak-ng-data/lang/gmw" "${OUTPUT}/espeak-ng-data/voices" "${OUTPUT}/dictsource")
file(COPY "${SOURCE}/espeak-ng-data/lang/gmw/en" "${SOURCE}/espeak-ng-data/lang/gmw/en-US"
    DESTINATION "${OUTPUT}/espeak-ng-data/lang/gmw")
file(COPY "${SOURCE}/phsource" DESTINATION "${OUTPUT}")
file(GLOB english_sources "${SOURCE}/dictsource/en_*")
file(COPY ${english_sources} DESTINATION "${OUTPUT}/dictsource")
foreach(step IN ITEMS --compile-intonations --compile-phonemes --compile=en)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "ESPEAK_DATA_PATH=${OUTPUT}" "${TOOL}" "${step}"
        WORKING_DIRECTORY "${OUTPUT}/dictsource" RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "eSpeak data compilation failed: ${step}")
    endif()
endforeach()
