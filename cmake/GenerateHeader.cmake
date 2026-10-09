# Runs generate.py and captures its stdout to OUTFILE.
#
# execute_process()'s OUTPUT_FILE redirects without needing a shell, so this
# works the same under every CMake generator (Ninja, Makefiles, Visual
# Studio, ...), unlike `COMMAND ... > file` in add_custom_command.
#
# Expects PYTHON3_EXECUTABLE, SRCDIR and OUTFILE to be passed via -D.

execute_process(
    COMMAND ${PYTHON3_EXECUTABLE} ${SRCDIR}/generate.py ${SRCDIR} ${SRCDIR}/libspectrum.h.in
    OUTPUT_FILE ${OUTFILE}
    RESULT_VARIABLE result
)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "generate.py failed with status ${result}")
endif()
