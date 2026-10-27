include(FetchContent)

# --- GoogleTest, pinned to a release AND verified by hash [REF-10] -----------
# (The reference project used GIT_TAG master: not reproducible.)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
  URL_HASH SHA256=40d4ec942217dcc84a9ebe2a68584ada7d4a33a8ee958755763278ea1c5e18ff
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(googletest)

# --- Klaus Dormann's functional test [REF-6] ----------------------------------
# GPL-3.0 licensed, so we do NOT copy it into this repository. We download one
# exact commit at configure time and refuse it if the hash differs. If the
# download fails (offline machine) the functional test is skipped, not broken.
set(KLAUS_COMMIT 7954e2dbb49c469ea286070bf46cdd71aeb29e4b)
set(KLAUS_BIN ${CMAKE_CURRENT_BINARY_DIR}/6502_functional_test.bin)
if(NOT EXISTS ${KLAUS_BIN})
  file(DOWNLOAD
    https://raw.githubusercontent.com/Klaus2m5/6502_65C02_functional_tests/${KLAUS_COMMIT}/bin_files/6502_functional_test.bin
    ${KLAUS_BIN}
    EXPECTED_HASH SHA256=fa12bfc761e6f9057e4cc01a665a7b800ff01ae91f598af1e39a1201d01953fd
    STATUS klaus_status)
  list(GET klaus_status 0 klaus_code)
  if(NOT klaus_code EQUAL 0)
    message(WARNING "Could not fetch Klaus functional test (${klaus_status}); that test will be skipped.")
    file(REMOVE ${KLAUS_BIN})
  endif()
endif()

add_executable(mos6502_tests
  test_support.hpp
  test_types_bus.cpp
  test_opcodes.cpp
  test_instructions.cpp
  test_arithmetic.cpp
  test_interrupts.cpp
  test_debug.cpp
  test_functional.cpp)
target_link_libraries(mos6502_tests PRIVATE mos6502 mos6502_debug GTest::gtest_main)
target_compile_definitions(mos6502_tests PRIVATE MOS6502_KLAUS_BIN="${KLAUS_BIN}")
mos6502_enable_warnings(mos6502_tests)
mos6502_enable_sanitizers(mos6502_tests)

include(GoogleTest)
gtest_discover_tests(mos6502_tests DISCOVERY_TIMEOUT 60)

# --- Bruce Clark's decimal-mode test [REF-5], assembled by tools/decimal_check ---
# Also GPL-3.0 (same repository), so it is downloaded rather than copied.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND AND MOS6502_BUILD_APPS)
  set(DECIMAL_SRC ${CMAKE_CURRENT_BINARY_DIR}/6502_decimal_test.a65)
  if(NOT EXISTS ${DECIMAL_SRC})
    file(DOWNLOAD
      https://raw.githubusercontent.com/Klaus2m5/6502_65C02_functional_tests/${KLAUS_COMMIT}/6502_decimal_test.a65
      ${DECIMAL_SRC}
      EXPECTED_HASH SHA256=dfbe4b907c5821d47d9f7f74eeb51197cf4b9f5260375bc1c6c48c008220e1a0
      STATUS decimal_status)
    list(GET decimal_status 0 decimal_code)
    if(NOT decimal_code EQUAL 0)
      message(WARNING "Could not fetch Clark's decimal test (${decimal_status}); skipping it.")
      file(REMOVE ${DECIMAL_SRC})
    endif()
  endif()
  if(EXISTS ${DECIMAL_SRC})
    add_test(NAME decimal_clark
      COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/decimal_check/check_decimal.py
              $<TARGET_FILE:run6502> ${DECIMAL_SRC})
  endif()
endif()
