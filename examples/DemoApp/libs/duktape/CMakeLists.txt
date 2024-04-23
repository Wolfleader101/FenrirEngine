project(duktape)

add_library(duktape STATIC)

target_include_directories(duktape SYSTEM PUBLIC src)

target_sources(duktape PRIVATE src/duktape.c)