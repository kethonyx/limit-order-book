# ASan + UBSan, opt-in via -DLOB_SANITIZE=ON. Must be applied to every target
# that links the library, so both compile and link flags are PUBLIC.
function(lob_enable_sanitizers target)
    if(NOT LOB_SANITIZE OR MSVC)
        return()
    endif()
    set(flags -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    target_compile_options(${target} PUBLIC ${flags})
    target_link_options(${target} PUBLIC ${flags})
endfunction()
