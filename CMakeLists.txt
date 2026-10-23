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
    src/cmd_common.c
    src/cmd_usage.c
    src/cmd_config.c
    src/cmd_watch.c
    src/cmd_analyze.c
    src/cli.c
    src/config.c
    src/error.c
    src/cancel.c
    src/i18n.c
    src/color.c
    src/anim.c
    src/chart.c
    src/usage.c
    src/tui.c
    src/models.c
    src/container.c
    src/perf.c
    src/platform/client.c
    src/platform/driver.c
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
    src/export/html.c
    src/export/csv.c
    src/export/xlsx.c
    src/export/pdf.c
    src/export/pdf_util.c
    src/export/pdf_font.c
    src/export/pdf_write.c
    src/export/pdf_doc.c
    src/export/pdf_objects.c
    src/server/api.c
    src/server/api_config.c
    src/server/api_jobs.c
    src/server/api_llm.c
    src/server/api_meta.c
    src/server/api_subs.c
    src/server/api_usage.c
    src/server/api_util.c
    src/server/jobs.c
    src/server/net.c
    src/server/respond.c
    src/server/serve.c
    src/server/static_files.c
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
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/third_party/cJSON
    ${CMAKE_SOURCE_DIR}/third_party/miniz
)
target_compile_definitions(gs PRIVATE MINIZ_NO_STDIO)
target_link_libraries(gs PRIVATE ${CURL_LIBRARIES} m z)

install(TARGETS gs RUNTIME DESTINATION bin)

# ---------------------------------------------------------------------------
# Web console
#
# `gs serve` serves the built console from --web-root, which defaults to
# <exe dir>/web and then ./web. This target builds the frontend and drops the
# result where the daemon looks first, so `gs serve` works with no flags after
# a normal build. It is opt-in so a C-only build never needs Node installed.
# ---------------------------------------------------------------------------
option(GITSIGHT_BUILD_WEB "Build the web console and bundle it next to gs" OFF)

if(GITSIGHT_BUILD_WEB)
  find_program(NPM_EXECUTABLE npm)
  if(NOT NPM_EXECUTABLE)
    message(FATAL_ERROR "GITSIGHT_BUILD_WEB is ON but npm was not found")
  endif()

  set(WEB_SRC ${CMAKE_SOURCE_DIR}/web)
  set(WEB_OUT ${CMAKE_BINARY_DIR}/web)

  add_custom_target(web ALL
    COMMAND ${NPM_EXECUTABLE} --prefix ${WEB_SRC} install --no-audit --no-fund
    COMMAND ${NPM_EXECUTABLE} --prefix ${WEB_SRC} run build
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${WEB_OUT}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${WEB_SRC}/dist ${WEB_OUT}
    WORKING_DIRECTORY ${WEB_SRC}
    COMMENT "Building the GitSight web console"
    VERBATIM)

  add_dependencies(gs web)
  install(DIRECTORY ${WEB_OUT} DESTINATION share/gitsight/web)
endif()

install(DIRECTORY ${CMAKE_SOURCE_DIR}/web/dist
        DESTINATION share/gitsight/web
        OPTIONAL)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME smoke COMMAND bash ${CMAKE_SOURCE_DIR}/tests/run_smoke.sh $<TARGET_FILE:gs> ${CMAKE_SOURCE_DIR})
    add_test(NAME completions COMMAND bash ${CMAKE_SOURCE_DIR}/tests/test_completions.sh $<TARGET_FILE:gs> ${CMAKE_SOURCE_DIR})
    add_test(NAME server COMMAND bash ${CMAKE_SOURCE_DIR}/tests/test_server.sh $<TARGET_FILE:gs> ${CMAKE_SOURCE_DIR})
    add_test(NAME cache COMMAND bash ${CMAKE_SOURCE_DIR}/tests/test_cache.sh $<TARGET_FILE:gs> ${CMAKE_SOURCE_DIR})
    # Static serving of the built console; all three skip when web/dist is absent.
    add_test(NAME web COMMAND bash ${CMAKE_SOURCE_DIR}/tests/test_web.sh $<TARGET_FILE:gs> ${CMAKE_SOURCE_DIR})
    add_test(NAME console COMMAND bash ${CMAKE_SOURCE_DIR}/tests/test_console.sh $<TARGET_FILE:gs> ${CMAKE_SOURCE_DIR})
    add_test(NAME smoke_console COMMAND bash ${CMAKE_SOURCE_DIR}/tests/test_smoke_console.sh $<TARGET_FILE:gs> ${CMAKE_SOURCE_DIR})
endif()
