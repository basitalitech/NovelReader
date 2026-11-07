# Weak references can let linking succeed while std::thread fails on hardware.
execute_process(COMMAND "${NM}" "${ELF}"
    RESULT_VARIABLE status OUTPUT_VARIABLE symbols ERROR_VARIABLE error)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "Could not inspect Vita thread linkage: ${error}")
endif()
foreach(symbol pthread_cancel pthread_once pthread_create pthread_join pthread_mutex_lock pthread_mutex_unlock)
    if(NOT symbols MATCHES "[0-9a-fA-F]+ [Tt] ${symbol}(\r?\n|$)")
        message(FATAL_ERROR "Vita executable is missing ${symbol}; link the complete pthread library.")
    endif()
endforeach()
