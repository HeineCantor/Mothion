# mothion_set_warnings(<target>): reasonable warning level on MSVC and GCC/Clang.
function(mothion_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
    endif()
endfunction()
