# Runs a Python accessor-generator script and captures its stdout to OUTFILE.
#
# execute_process()'s OUTPUT_FILE redirects without needing a shell, so this
# works the same under every CMake generator (Ninja, Makefiles, Visual
# Studio, ...), unlike `COMMAND ... > file` in add_custom_command.
#
# Expects PYTHON3_EXECUTABLE, SCRIPT, TXTFILE and OUTFILE to be passed via -D.

execute_process(
    COMMAND ${PYTHON3_EXECUTABLE} ${SCRIPT} ${TXTFILE}
    OUTPUT_FILE ${OUTFILE}
    RESULT_VARIABLE result
)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "${SCRIPT} failed with status ${result}")
endif()
