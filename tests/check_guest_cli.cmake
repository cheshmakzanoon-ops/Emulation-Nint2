# Require both a successful guest exit and the complete expected stdout.
# CTest PASS_REGULAR_EXPRESSION alone can accept a nonzero process exit.
foreach(required NINT2_EXECUTABLE GUEST_ELF EXPECTED_OUTPUT)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "Missing required parameter: ${required}")
  endif()
endforeach()

execute_process(
  COMMAND "${NINT2_EXECUTABLE}" --run-synthetic "${GUEST_ELF}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
  TIMEOUT 30
)
if(NOT "${result}" STREQUAL "0")
  message(FATAL_ERROR "Guest CLI failed (${result}):\n${error}\nstdout:\n${output}")
endif()

# Windows text-mode output uses CRLF; retain all other bytes and the final
# newline so truncated, extra, or partially successful output cannot pass.
string(REPLACE "\r\n" "\n" output "${output}")
set(expected "${EXPECTED_OUTPUT}\n")
if(NOT "${output}" STREQUAL "${expected}")
  message(FATAL_ERROR "Guest CLI stdout mismatch.\nExpected:\n${expected}\nActual:\n${output}")
endif()
