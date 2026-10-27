# Warning and sanitizer helpers, applied per target (never globally).

function(mos6502_enable_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
    if(MOS6502_WERROR)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
      -Wold-style-cast -Wcast-align -Wnon-virtual-dtor -Woverloaded-virtual
      -Wnull-dereference -Wdouble-promotion -Wformat=2)
    if(MOS6502_WERROR)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

function(mos6502_enable_sanitizers target)
  if(MOS6502_SANITIZE AND NOT MSVC)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()
