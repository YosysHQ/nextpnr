# This is checking for CMAKE_CXX_COMPILER's ability to use `#embed`.
# BBAsm is using `#embed` (C) and not `std::embed` (C++) even though it emits C++ code.
# As of 2025-01-16: Note that the usage of `#embed` in C has different rules (the C++ one is
# an extension, the C one is in the C23 standard).

# Example usage:
#
# check_cxx_compiler_hash_embed(HAS_HASH_EMBED CXX_FLAGS_HASH_EMBED)
# set(CMAKE_CXX_FLAGS ${CXX_FLAGS_HASH_EMBED} ${CMAKE_CXX_FLAGS})
#
function(check_cxx_compiler_hash_embed VAR FLAGS_VAR)
    try_compile(
        ${VAR}
        SOURCE_FROM_CONTENT
            compiletest.cc
            "const char s[] = {\n#embed \"${CMAKE_CURRENT_FUNCTION_LIST_FILE}\"\n};\nint main() {}"
    )
    if (${VAR})
        if (FLAGS_VAR)
            check_cxx_compiler_flag(-Wc23-extensions HAS_Wc23-extensions)
            if (HAS_Wc23-extensions)
                set(${FLAGS_VAR} -Wno-c23-extensions PARENT_SCOPE)
            endif()
        endif()

        message(STATUS "C++ compiler supports #embed")
    else()
        message(STATUS "C++ compiler does NOT support #embed")
    endif()
endfunction()
