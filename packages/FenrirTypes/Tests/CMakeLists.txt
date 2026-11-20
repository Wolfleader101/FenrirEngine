# FenrirTypes's own tests, on hand-written descriptions and on the registration tools/generate-type-registry writes from
# describe-api's fixture (Generated/), through its public interface only. No other engine package.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

# Every public header compiles alone: one source per header, holding nothing but its #include.
file(GLOB fenrir_types_headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../include/FenrirTypes/*.hpp")
set(fenrir_types_header_checks)
foreach(header IN LISTS fenrir_types_headers)
    get_filename_component(header_name "${header}" NAME)
    get_filename_component(header_stem "${header}" NAME_WE)
    set(check "${CMAKE_CURRENT_BINARY_DIR}/PublicHeaders/${header_stem}.cpp")
    file(CONFIGURE OUTPUT "${check}" CONTENT "#include \"FenrirTypes/${header_name}\"\n")
    list(APPEND fenrir_types_header_checks "${check}")
endforeach()

add_executable(FenrirTypesTests PublicHeaders.cpp TestTypes.hpp Registry.cpp Attachments.cpp Visiting.cpp Values.cpp
                                Describe.cpp GeneratedFixture.cpp Generated/FixtureTypes.hpp Generated/FixtureTypes.cpp
                                ${fenrir_types_header_checks})
# The fixture header the generated registration is compiled against: describe-api's, from its include directory.
target_link_libraries(FenrirTypesTests PRIVATE FenrirTypes DescribeApiFixture Catch2::Catch2WithMain)
add_test(NAME FenrirTypes COMMAND FenrirTypesTests)
