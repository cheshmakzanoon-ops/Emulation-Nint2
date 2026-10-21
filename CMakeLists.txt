cmake_minimum_required(VERSION 3.24)
project(EmulationNint2 VERSION 0.1.0 LANGUAGES CXX)
option(NINT2_BUILD_TESTS "Build standalone foundation tests" ON)
add_library(nint2_core STATIC src/core/memory.cpp src/core/cpu.cpp src/core/cpu_extended.cpp src/core/cpu_atomic.cpp src/core/cpu_lse.cpp src/core/cpu_system.cpp src/core/cpu_pair.cpp src/core/cpu_crc.cpp src/core/elf.cpp src/core/session.cpp src/core/kernel.cpp src/core/scheduler.cpp src/core/thread_group.cpp src/core/assets.cpp src/core/arena.cpp)
target_include_directories(nint2_core PUBLIC include)
target_compile_features(nint2_core PUBLIC cxx_std_20)
set_target_properties(nint2_core PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
  target_compile_options(nint2_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(nint2_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_executable(nint2 src/app/main.cpp src/app/synthetic_pair.cpp)
target_link_libraries(nint2 PRIVATE nint2_core)
if(NINT2_BUILD_TESTS)
  enable_testing()
  add_executable(nint2_vm_guest_tests tests/vm_guest_tests.cpp)
  target_link_libraries(nint2_vm_guest_tests PRIVATE nint2_core)
  add_test(NAME synthetic_arm64_arena_integration COMMAND nint2_vm_guest_tests)
  add_test(NAME generate_original_vm_elf COMMAND nint2_vm_guest_tests --emit-elf
           "${CMAKE_CURRENT_BINARY_DIR}/original_vm_guest.elf")
  set_tests_properties(generate_original_vm_elf PROPERTIES FIXTURES_SETUP original_vm_elf)
  add_executable(nint2_asset_guest_tests tests/asset_guest_tests.cpp)
  target_link_libraries(nint2_asset_guest_tests PRIVATE nint2_core)
  add_test(NAME synthetic_arm64_asset_integration COMMAND nint2_asset_guest_tests)
  add_test(NAME generate_original_asset_elf COMMAND nint2_asset_guest_tests --emit-elf
           "${CMAKE_CURRENT_BINARY_DIR}/original_asset_guest.elf")
  set_tests_properties(generate_original_asset_elf PROPERTIES FIXTURES_SETUP original_asset_elf)
  add_test(NAME cli_original_asset_guest COMMAND "${CMAKE_COMMAND}"
           "-DNINT2_EXECUTABLE=$<TARGET_FILE:nint2>"
           "-DGUEST_ELF=${CMAKE_CURRENT_BINARY_DIR}/original_asset_guest.elf"
           "-DHOST_ASSET=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/asset_message.txt"
           "-DEXPECTED_OUTPUT=Loaded from asset!"
           -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_asset_cli.cmake")
  set_tests_properties(cli_original_asset_guest PROPERTIES
      FIXTURES_REQUIRED original_asset_elf TIMEOUT 30)
  add_test(NAME cli_rejects_invalid_guest_asset_path COMMAND nint2 --run-synthetic-asset
           "${CMAKE_CURRENT_BINARY_DIR}/original_asset_guest.elf" ../escape
           "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/asset_message.txt")
  set_tests_properties(cli_rejects_invalid_guest_asset_path PROPERTIES
      FIXTURES_REQUIRED original_asset_elf WILL_FAIL TRUE)
  add_executable(nint2_heap_guest_tests tests/heap_guest_tests.cpp)
  target_link_libraries(nint2_heap_guest_tests PRIVATE nint2_core)
  add_test(NAME synthetic_original_arm64_heap_asset COMMAND nint2_heap_guest_tests)
  add_test(NAME generate_original_heap_guest_elf COMMAND nint2_heap_guest_tests --emit-elf
           "${CMAKE_CURRENT_BINARY_DIR}/original_heap_guest.elf")
  set_tests_properties(generate_original_heap_guest_elf PROPERTIES
      FIXTURES_SETUP original_heap_elf)
  add_test(NAME cli_original_heap_guest COMMAND "${CMAKE_COMMAND}"
           "-DNINT2_EXECUTABLE=$<TARGET_FILE:nint2>"
           "-DGUEST_ELF=${CMAKE_CURRENT_BINARY_DIR}/original_heap_guest.elf"
           "-DHOST_ASSET=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/asset_message.txt"
           "-DEXPECTED_OUTPUT=Loaded from asset!"
           -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_asset_cli.cmake")
  set_tests_properties(cli_original_heap_guest PROPERTIES
      FIXTURES_REQUIRED original_heap_elf TIMEOUT 30)
  add_executable(nint2_arena_tests tests/arena_tests.cpp)
  target_link_libraries(nint2_arena_tests PRIVATE nint2_core)
  add_test(NAME synthetic_virtual_memory_arena COMMAND nint2_arena_tests)
  add_executable(nint2_assets_tests tests/assets_tests.cpp)
  target_link_libraries(nint2_assets_tests PRIVATE nint2_core)
  add_test(NAME synthetic_asset_store COMMAND nint2_assets_tests)
  add_executable(nint2_memory_pair_tests tests/memory_pair_tests.cpp)
  target_link_libraries(nint2_memory_pair_tests PRIVATE nint2_core)
  add_test(NAME guest_memory_atomic_pairs COMMAND nint2_memory_pair_tests)
  add_executable(nint2_memory_atomic_tests tests/memory_atomic_tests.cpp)
  target_link_libraries(nint2_memory_atomic_tests PRIVATE nint2_core)
  add_test(NAME guest_memory_lse_atomic_differential COMMAND nint2_memory_atomic_tests)
  add_executable(nint2_memory_tests tests/memory_tests.cpp)
  target_link_libraries(nint2_memory_tests PRIVATE nint2_core)
  add_test(NAME guest_memory COMMAND nint2_memory_tests)
  add_executable(nint2_memory_bulk_tests tests/memory_bulk_tests.cpp)
  target_link_libraries(nint2_memory_bulk_tests PRIVATE nint2_core)
  add_test(NAME guest_memory_bulk_transactional COMMAND nint2_memory_bulk_tests)
  add_test(NAME cli_smoke COMMAND nint2 --smoke)
  add_executable(nint2_cpu_extended_tests tests/cpu_extended_tests.cpp)
  target_link_libraries(nint2_cpu_extended_tests PRIVATE nint2_core)
  add_test(NAME aarch64_extended_integer COMMAND nint2_cpu_extended_tests)
  add_executable(nint2_cpu_atomic_tests tests/cpu_atomic_tests.cpp)
  target_link_libraries(nint2_cpu_atomic_tests PRIVATE nint2_core)
  add_test(NAME aarch64_exclusive_atomic_monitor COMMAND nint2_cpu_atomic_tests)
  add_executable(nint2_thread_group_tests tests/thread_group_tests.cpp)
  target_link_libraries(nint2_thread_group_tests PRIVATE nint2_core)
  add_test(NAME synthetic_aarch64_thread_group COMMAND nint2_thread_group_tests)
  add_executable(nint2_signed_load_tests tests/cpu_signed_load_tests.cpp)
  target_link_libraries(nint2_signed_load_tests PRIVATE nint2_core)
  add_test(NAME aarch64_signed_loads COMMAND nint2_signed_load_tests)
  add_executable(nint2_bit_operations_tests tests/cpu_bit_operations_tests.cpp)
  target_link_libraries(nint2_bit_operations_tests PRIVATE nint2_core)
  add_test(NAME aarch64_bit_operations COMMAND nint2_bit_operations_tests)
  add_executable(nint2_logical_tests tests/cpu_logical_tests.cpp)
  target_link_libraries(nint2_logical_tests PRIVATE nint2_core)
  add_test(NAME aarch64_logical_immediates COMMAND nint2_logical_tests)
  add_executable(nint2_sp_logical_tests tests/cpu_sp_logical_tests.cpp)
  target_link_libraries(nint2_sp_logical_tests PRIVATE nint2_core)
  add_test(NAME aarch64_sp_logical_operands COMMAND nint2_sp_logical_tests)
  add_executable(nint2_extract_tests tests/cpu_extract_tests.cpp)
  target_link_libraries(nint2_extract_tests PRIVATE nint2_core)
  add_test(NAME aarch64_extract_rotate_immediate COMMAND nint2_extract_tests)
  add_executable(nint2_carry_tests tests/cpu_carry_tests.cpp)
  target_link_libraries(nint2_carry_tests PRIVATE nint2_core)
  add_test(NAME aarch64_add_subtract_carry COMMAND nint2_carry_tests)
  add_executable(nint2_divide_shift_tests tests/cpu_divide_shift_tests.cpp)
  target_link_libraries(nint2_divide_shift_tests PRIVATE nint2_core)
  add_test(NAME aarch64_divide_variable_shifts COMMAND nint2_divide_shift_tests)
  add_executable(nint2_system_tests tests/cpu_system_tests.cpp)
  target_link_libraries(nint2_system_tests PRIVATE nint2_core)
  add_test(NAME aarch64_user_system_registers COMMAND nint2_system_tests)
  add_executable(nint2_pair_tests tests/cpu_pair_tests.cpp)
  target_link_libraries(nint2_pair_tests PRIVATE nint2_core)
  add_test(NAME aarch64_paired_atomic_conformance COMMAND nint2_pair_tests)
  add_executable(nint2_lse_tests tests/cpu_lse_tests.cpp)
  target_link_libraries(nint2_lse_tests PRIVATE nint2_core)
  add_test(NAME aarch64_lse_atomic_conformance COMMAND nint2_lse_tests)
  add_executable(nint2_exclusive_tests tests/cpu_exclusive_tests.cpp)
  target_link_libraries(nint2_exclusive_tests PRIVATE nint2_core)
  add_test(NAME aarch64_exclusive_atomic_regressions COMMAND nint2_exclusive_tests)
  add_executable(nint2_cpu_tests tests/cpu_tests.cpp)
  target_link_libraries(nint2_cpu_tests PRIVATE nint2_core)
  add_test(NAME aarch64_reference COMMAND nint2_cpu_tests)
  add_test(NAME cpu_smoke COMMAND nint2 --cpu-smoke)
  add_executable(nint2_elf_tests tests/elf_tests.cpp)
  target_link_libraries(nint2_elf_tests PRIVATE nint2_core)
  add_test(NAME original_elf64 COMMAND nint2_elf_tests)
  add_executable(nint2_pie_elf_tests tests/pie_elf_tests.cpp)
  target_link_libraries(nint2_pie_elf_tests PRIVATE nint2_core)
  add_test(NAME original_aarch64_static_pie COMMAND nint2_pie_elf_tests)
  add_executable(nint2_pie_rela_stress_tests tests/pie_relocation_stress_tests.cpp)
  target_link_libraries(nint2_pie_rela_stress_tests PRIVATE nint2_core)
  add_test(NAME aarch64_pie_rela_stress COMMAND nint2_pie_rela_stress_tests)
  add_executable(nint2_session_tests tests/session_tests.cpp)
  target_link_libraries(nint2_session_tests PRIVATE nint2_core)
  add_test(NAME original_guest_session COMMAND nint2_session_tests)
  add_executable(nint2_multi_guest_elf_tests tests/multi_guest_elf_tests.cpp)
  target_link_libraries(nint2_multi_guest_elf_tests PRIVATE nint2_core)
  add_test(NAME original_multi_guest_elf COMMAND nint2_multi_guest_elf_tests)
  add_test(NAME emit_original_multi_guest_elf COMMAND nint2_multi_guest_elf_tests
           --emit-elf "${CMAKE_CURRENT_BINARY_DIR}/original_sender.elf"
                      "${CMAKE_CURRENT_BINARY_DIR}/original_receiver.elf")
  set_tests_properties(emit_original_multi_guest_elf PROPERTIES FIXTURES_SETUP original_multi_elf)
  add_test(NAME reload_original_multi_guest_elf COMMAND nint2_multi_guest_elf_tests
           --run-pair "${CMAKE_CURRENT_BINARY_DIR}/original_sender.elf"
                      "${CMAKE_CURRENT_BINARY_DIR}/original_receiver.elf")
  set_tests_properties(reload_original_multi_guest_elf PROPERTIES FIXTURES_REQUIRED original_multi_elf)
  add_executable(nint2_scheduler_tests tests/scheduler_tests.cpp)
  target_link_libraries(nint2_scheduler_tests PRIVATE nint2_core)
  add_test(NAME synthetic_multi_guest_scheduler COMMAND nint2_scheduler_tests)
  add_executable(nint2_kernel_tests tests/kernel_tests.cpp)
  target_link_libraries(nint2_kernel_tests PRIVATE nint2_core)
  add_test(NAME kernel_handles_ipc COMMAND nint2_kernel_tests)
  add_executable(nint2_queue_wait_tests tests/queue_wait_tests.cpp)
  target_link_libraries(nint2_queue_wait_tests PRIVATE nint2_core)
  add_test(NAME synthetic_queue_wait_deadlock COMMAND nint2_queue_wait_tests)
  add_executable(nint2_kernel_liveness_tests tests/kernel_liveness_tests.cpp)
  target_link_libraries(nint2_kernel_liveness_tests PRIVATE nint2_core)
  add_test(NAME kernel_queue_lifecycle COMMAND nint2_kernel_liveness_tests)
  add_executable(nint2_kernel_resource_tests tests/kernel_resource_tests.cpp)
  target_link_libraries(nint2_kernel_resource_tests PRIVATE nint2_core)
  add_test(NAME kernel_allocation_failures COMMAND nint2_kernel_resource_tests)
  add_executable(nint2_session_import_tests tests/session_import_tests.cpp)
  target_link_libraries(nint2_session_import_tests PRIVATE nint2_core)
  add_test(NAME synthetic_host_queue_imports COMMAND nint2_session_import_tests)
  add_executable(nint2_compiled_rpc_tests tests/compiled_rpc_guest_tests.cpp)
  target_link_libraries(nint2_compiled_rpc_tests PRIVATE nint2_core)
  add_test(NAME original_compiled_c_rpc COMMAND nint2_compiled_rpc_tests)
  add_test(NAME generate_compiled_rpc_elf COMMAND nint2_compiled_rpc_tests --emit-elf
           "${CMAKE_CURRENT_BINARY_DIR}/compiled_rpc_client.elf"
           "${CMAKE_CURRENT_BINARY_DIR}/compiled_rpc_server.elf")
  set_tests_properties(generate_compiled_rpc_elf PROPERTIES FIXTURES_SETUP original_rpc_elf)
  add_test(NAME cli_compiled_rpc_pair COMMAND "${CMAKE_COMMAND}"
           "-DNINT2_EXECUTABLE=$<TARGET_FILE:nint2>"
           "-DCLIENT_ELF=${CMAKE_CURRENT_BINARY_DIR}/compiled_rpc_client.elf"
           "-DSERVER_ELF=${CMAKE_CURRENT_BINARY_DIR}/compiled_rpc_server.elf"
           -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_rpc_cli.cmake")
  set_tests_properties(cli_compiled_rpc_pair PROPERTIES FIXTURES_REQUIRED original_rpc_elf)
  add_test(NAME cli_rpc_rejects_invalid_elf COMMAND nint2 --run-synthetic-pair
           "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
           "${CMAKE_CURRENT_BINARY_DIR}/compiled_rpc_server.elf")
  set_tests_properties(cli_rpc_rejects_invalid_elf PROPERTIES
      FIXTURES_REQUIRED original_rpc_elf WILL_FAIL TRUE)
  add_executable(nint2_compiled_lse_tests tests/compiled_lse_guest_tests.cpp)
  target_link_libraries(nint2_compiled_lse_tests PRIVATE nint2_core)
  add_test(NAME original_compiled_c_lse_atomic_guest COMMAND nint2_compiled_lse_tests)
  add_test(NAME generate_compiled_lse_elf COMMAND nint2_compiled_lse_tests --emit-elf
           "${CMAKE_CURRENT_BINARY_DIR}/compiled_lse_guest.elf")
  set_tests_properties(generate_compiled_lse_elf PROPERTIES FIXTURES_SETUP compiled_lse_elf)
  add_test(NAME cli_original_compiled_lse_guest COMMAND "${CMAKE_COMMAND}"
           "-DNINT2_EXECUTABLE=$<TARGET_FILE:nint2>"
           "-DGUEST_ELF=${CMAKE_CURRENT_BINARY_DIR}/compiled_lse_guest.elf"
           "-DEXPECTED_OUTPUT=Compiled ARM64 LSE atomics verified!"
           -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_guest_cli.cmake")
  set_tests_properties(cli_original_compiled_lse_guest PROPERTIES
      FIXTURES_REQUIRED compiled_lse_elf TIMEOUT 30)
  add_executable(nint2_compiled_c_tests tests/compiled_c_guest_tests.cpp)
  target_link_libraries(nint2_compiled_c_tests PRIVATE nint2_core)
  add_test(NAME original_compiled_c_guest COMMAND nint2_compiled_c_tests)
  add_executable(nint2_compiled_ipc_tests tests/compiled_ipc_guest_tests.cpp)
  target_link_libraries(nint2_compiled_ipc_tests PRIVATE nint2_core)
  add_test(NAME original_compiled_c_ipc COMMAND nint2_compiled_ipc_tests)
  add_test(NAME generate_compiled_ipc_elf COMMAND nint2_compiled_ipc_tests --emit-elf
           "${CMAKE_CURRENT_BINARY_DIR}/compiled_ipc_guest.elf")
  set_tests_properties(generate_compiled_ipc_elf PROPERTIES FIXTURES_SETUP authored_compiled_ipc_elf)
  add_test(NAME cli_compiled_ipc_guest COMMAND "${CMAKE_COMMAND}"
           "-DNINT2_EXECUTABLE=$<TARGET_FILE:nint2>"
           "-DGUEST_ELF=${CMAKE_CURRENT_BINARY_DIR}/compiled_ipc_guest.elf"
           "-DEXPECTED_OUTPUT=C guest IPC verified!"
           -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_guest_cli.cmake")
  set_tests_properties(cli_compiled_ipc_guest PROPERTIES
      FIXTURES_REQUIRED authored_compiled_ipc_elf TIMEOUT 30)
  add_test(NAME generate_compiled_c_elf COMMAND nint2_compiled_c_tests --emit-elf
           "${CMAKE_CURRENT_BINARY_DIR}/compiled_c_guest.elf")
  set_tests_properties(generate_compiled_c_elf PROPERTIES FIXTURES_SETUP authored_compiled_c_elf)
  add_test(NAME cli_compiled_c_guest COMMAND "${CMAKE_COMMAND}"
           "-DNINT2_EXECUTABLE=$<TARGET_FILE:nint2>"
           "-DGUEST_ELF=${CMAKE_CURRENT_BINARY_DIR}/compiled_c_guest.elf"
           "-DEXPECTED_OUTPUT=Compiled C on AArch64!"
           -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_guest_cli.cmake")
  set_tests_properties(cli_compiled_c_guest PROPERTIES
      FIXTURES_REQUIRED authored_compiled_c_elf TIMEOUT 30)
  add_executable(nint2_ipc_guest_tests tests/ipc_guest_tests.cpp)
  target_link_libraries(nint2_ipc_guest_tests PRIVATE nint2_core)
  add_test(NAME original_guest_ipc COMMAND nint2_ipc_guest_tests)
  add_test(NAME generate_ipc_guest_elf COMMAND nint2_ipc_guest_tests --emit-elf
           "${CMAKE_CURRENT_BINARY_DIR}/synthetic_ipc.elf")
  set_tests_properties(generate_ipc_guest_elf PROPERTIES FIXTURES_SETUP authored_ipc_elf)
  add_test(NAME cli_guest_ipc COMMAND "${CMAKE_COMMAND}"
           "-DNINT2_EXECUTABLE=$<TARGET_FILE:nint2>"
           "-DGUEST_ELF=${CMAKE_CURRENT_BINARY_DIR}/synthetic_ipc.elf"
           "-DEXPECTED_OUTPUT=Hello via kernel!"
           -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_guest_cli.cmake")
  set_tests_properties(cli_guest_ipc PROPERTIES
      FIXTURES_REQUIRED authored_ipc_elf TIMEOUT 30)
  add_test(NAME generate_guest_elf COMMAND nint2_session_tests --emit-elf
           "${CMAKE_CURRENT_BINARY_DIR}/synthetic_hello.elf")
  set_tests_properties(generate_guest_elf PROPERTIES FIXTURES_SETUP authored_elf)
  add_test(NAME cli_guest_elf COMMAND "${CMAKE_COMMAND}"
           "-DNINT2_EXECUTABLE=$<TARGET_FILE:nint2>"
           "-DGUEST_ELF=${CMAKE_CURRENT_BINARY_DIR}/synthetic_hello.elf"
           "-DEXPECTED_OUTPUT=Hello from AArch64!"
           -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_guest_cli.cmake")
  set_tests_properties(cli_guest_elf PROPERTIES
      FIXTURES_REQUIRED authored_elf TIMEOUT 30)
endif()
