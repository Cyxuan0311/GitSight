cmake_minimum_required(VERSION 3.14)
project(gitsight C)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)

set(GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
set(COMPLETIONS_EMBED ${GENERATED_DIR}/completions_embed.c)

add_custom_command(
    OUTPUT ${COMPLETIONS_EMBED}
    COMMAND ${CMAKE_COMMAND}
        -DBASH=${CMAKE_SOURCE_DIR}/completions/gs.bash
        -DZSH=${CMAKE_SOURCE_DIR}/completions/_gs
        -DFISH=${CMAKE_SOURCE_DIR}/completions/gs.fish
        -DOUT=${COMPLETIONS_EMBED}
        -P ${CMAKE_SOURCE_DIR}/scripts/embed_completions.cmake
    DEPENDS
        ${CMAKE_SOURCE_DIR}/completions/gs.bash
        ${CMAKE_SOURCE_DIR}/completions/_gs
        ${CMAKE_SOURCE_DIR}/completions/gs.fish
        ${CMAKE_SOURCE_DIR}/scripts/embed_completions.cmake
    VERBATIM
)

set(SOURCES
    src/main.c
    src/cli.c
    src/config.c
    src/error.c
    src/color.c
    src/anim.c
    src/chart.c
    src/usage.c
    src/tui.c
    src/models.c
    src/container.c
    src/platform/client.c
    src/platform/http.c
    src/platform/profile.c
    src/platform/github.c
    src/platform/gitlab.c
    src/analyzer/engine.c
    src/llm/http.c
    src/llm/dispatch.c
    src/llm/openai.c
    src/llm/anthropic.c
    src/llm/gemini.c
    src/export/export.c
    src/export/json.c
    src/export/md.c
    src/export/txt.c
    src/export/xlsx.c
    src/export/pdf.c
    ${COMPLETIONS_EMBED}
    third_party/cJSON/cJSON.c
    third_party/miniz/miniz.c
    third_party/miniz/miniz_tdef.c
    third_party/miniz/miniz_tinfl.c
    third_party/miniz/miniz_zip.c
)

add_executable(gs ${SOURCES})
target_include_directories(gs PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/third_party/cJSON
    ${CMAKE_SOURCE_DIR}/third_party/miniz
)
target_compile_definitions(gs PRIVATE MINIZ_NO_STDIO)
target_link_libraries(gs PRIVATE ${CURL_LIBRARIES} m z)

install(TARGETS gs RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME smoke COMMAND bash ${CMAKE_SOURCE_DIR}/tests/run_smoke.sh $<TARGET_FILE:gs> ${CMAKE_SOURCE_DIR})
    add_test(NAME completions COMMAND bash ${CMAKE_SOURCE_DIR}/tests/test_completions.sh $<TARGET_FILE:gs> ${CMAKE_SOURCE_DIR})
endif()
