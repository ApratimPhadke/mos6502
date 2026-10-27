# Optional: compare against the original reference emulator (davepoo/6502Emulator).
# Configure with  -DMOS6502_REFERENCE_DIR=/path/to/6502Emulator-master/6502/6502Lib/src
add_library(ref6502 STATIC ${MOS6502_REFERENCE_DIR}/private/m6502.cpp)
target_include_directories(ref6502 PUBLIC ${MOS6502_REFERENCE_DIR}/public)
target_compile_options(ref6502 PRIVATE -w)   # third-party code: not our warnings

add_executable(compare_reference compare_reference.cpp)
target_link_libraries(compare_reference PRIVATE mos6502 ref6502)
target_compile_options(compare_reference PRIVATE -Wall -Wextra)
