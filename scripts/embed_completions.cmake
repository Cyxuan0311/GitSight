# embed_completions.cmake
# Converts the shell completion scripts in completions/ into C string literals.
# Invoked by CMake at build time:
#   cmake -DBASH=<path> -DZSH=<path> -DFISH=<path> -DOUT=<path> -P embed_completions.cmake

function(embed_script var file)
    file(READ "${file}" content)
    string(REPLACE "\\" "\\\\" content "${content}")
    string(REPLACE "\"" "\\\"" content "${content}")
    string(REPLACE "\n" "\\n" content "${content}")
    string(REPLACE "\r" "\\r" content "${content}")
    set("${var}" "${content}" PARENT_SCOPE)
endfunction()

embed_script(bash_script "${BASH}")
embed_script(zsh_script "${ZSH}")
embed_script(fish_script "${FISH}")

file(WRITE "${OUT}"
"#include \"gitsight/completions.h\"\n"
"\n"
"/* Scripts are >4095 chars, above the ISO C99 translation limit that\n"
" * -Woverlength-strings warns about; the literal itself is fine to use. */\n"
"#if defined(__GNUC__) && !defined(__clang__)\n"
"#pragma GCC diagnostic ignored \"-Woverlength-strings\"\n"
"#endif\n"
"\n"
"const char gs_completion_bash[] =\n"
"\"${bash_script}\"\n"
";\n"
"\n"
"const char gs_completion_zsh[] =\n"
"\"${zsh_script}\"\n"
";\n"
"\n"
"const char gs_completion_fish[] =\n"
"\"${fish_script}\"\n"
";\n"
)
