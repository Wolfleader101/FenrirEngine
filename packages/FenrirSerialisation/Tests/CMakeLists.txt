# FenrirSerialisation's tests, through its public interface only. The goldens in fixtures/ are the JSON5 samples of
# the format research (branch research/format-fit, a78ddc75), committed as they were and never regenerated, and
# fixtures/json5-tests/ is the json5-tests suite (MIT), its README naming the commit taken.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

# Every public header compiles alone: one source per header, holding nothing but its #include.
file(GLOB fenrir_serialisation_headers CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/../include/FenrirSerialisation/*.hpp")
set(fenrir_serialisation_header_checks)
foreach(header IN LISTS fenrir_serialisation_headers)
    get_filename_component(header_name "${header}" NAME)
    get_filename_component(header_stem "${header}" NAME_WE)
    set(check "${CMAKE_CURRENT_BINARY_DIR}/PublicHeaders/${header_stem}.cpp")
    file(CONFIGURE OUTPUT "${check}" CONTENT "#include \"FenrirSerialisation/${header_name}\"\n")
    list(APPEND fenrir_serialisation_header_checks "${check}")
endforeach()

add_executable(FenrirSerialisationTests PublicHeaders.cpp Values.cpp WriteText.cpp ReadText.cpp Json5Tests.cpp
                                        Goldens.cpp SerialisationTest.hpp ${fenrir_serialisation_header_checks})
target_sources(FenrirSerialisationTests PRIVATE Encoding.cpp)
target_link_libraries(FenrirSerialisationTests PRIVATE FenrirSerialisation Catch2::Catch2WithMain)
target_compile_definitions(FenrirSerialisationTests
                           PRIVATE FENRIR_SERIALISATION_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME FenrirSerialisation COMMAND FenrirSerialisationTests)
