# Test the real CLI's exit status and exact output independently. A CTest
# PASS_REGULAR_EXPRESSION alone would ignore a failing process exit code.
execute_process(
    COMMAND "${NINT2_EXECUTABLE}" --run-synthetic-pair "${CLIENT_ELF}" "${SERVER_ELF}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE diagnostics
    TIMEOUT 30)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "RPC CLI failed (${result}): ${diagnostics}")
endif()
# C++ text-mode stdout uses CRLF on Windows.
string(REPLACE "\r\n" "\n" output "${output}")
set(expected "C RPC client: 64 replies verified!\nC RPC server: 64 requests processed!\n")
if(NOT "${output}" STREQUAL "${expected}")
    message(FATAL_ERROR "RPC CLI output mismatch: ${output}\n${diagnostics}")
endif()
message(STATUS "${output}${diagnostics}")
