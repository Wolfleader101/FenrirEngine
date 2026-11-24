# FenrirCore's own tests, through its public headers only. No App, no file, no clock: the reload watcher is told the
# time by the test.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

# Every public header compiles alone: one source per header, holding nothing but its #include.
file(GLOB fenrir_core_headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../include/FenrirCore/*.hpp")
set(fenrir_core_header_checks)
foreach(header IN LISTS fenrir_core_headers)
    get_filename_component(header_name "${header}" NAME)
    get_filename_component(header_stem "${header}" NAME_WE)
    set(check "${CMAKE_CURRENT_BINARY_DIR}/PublicHeaders/${header_stem}.cpp")
    file(CONFIGURE OUTPUT "${check}" CONTENT "#include \"FenrirCore/${header_name}\"\n")
    list(APPEND fenrir_core_header_checks "${check}")
endforeach()

add_executable(FenrirCoreTests PublicHeaders.cpp ReloadWatcherTests.cpp ${fenrir_core_header_checks})
target_link_libraries(FenrirCoreTests PRIVATE FenrirCore Catch2::Catch2WithMain)
add_test(NAME FenrirCore COMMAND FenrirCoreTests --order decl)
