# Require exact guest stdout and success, not just a matching regex.
foreach(key NINT2_EXECUTABLE GUEST_ELF HOST_ASSET EXPECTED_OUTPUT)
  if(NOT DEFINED ${key})
    message(FATAL_ERROR "Missing required argument: ${key}")
  endif()
endforeach()
execute_process(
  COMMAND "${NINT2_EXECUTABLE}" --run-synthetic-asset "${GUEST_ELF}"
          /demo/message "${HOST_ASSET}"
  RESULT_VARIABLE exit_status
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
  TIMEOUT 30)
if(NOT "${exit_status}" STREQUAL "0")
  message(FATAL_ERROR "Asset guest failed: ${exit_status}\n${error}\n${output}")
endif()
string(REPLACE "\r\n" "\n" output "${output}")
set(expected "${EXPECTED_OUTPUT}\n")
if(NOT "${output}" STREQUAL "${expected}")
  message(FATAL_ERROR "Asset guest output mismatch:\nExpected: ${expected}\nActual: ${output}")
endif()
