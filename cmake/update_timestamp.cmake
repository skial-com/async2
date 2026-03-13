# Generate build_timestamp.h with current time including UTC offset.
# Runs at build time (not configure time) via add_custom_target.

if(WIN32)
    execute_process(
        COMMAND cmd /c "powershell -NoProfile -Command \"Get-Date -Format 'MMM dd yyyy HH:mm:ss zzz'\""
        OUTPUT_VARIABLE _now
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    execute_process(
        COMMAND date "+%b %d %Y %H:%M:%S %z"
        OUTPUT_VARIABLE _now
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

if(NOT _now)
    string(TIMESTAMP _now "%b %d %Y %H:%M:%S UTC")
endif()

file(WRITE "${OUTPUT_FILE}" "#define BUILD_TIMESTAMP \"${_now}\"\n")
execute_process(COMMAND ${CMAKE_COMMAND} -E touch "${SOURCE_FILE}")
