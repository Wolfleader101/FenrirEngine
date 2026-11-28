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
target_sources(FenrirSerialisationTests PRIVATE ToTree.cpp BridgeTypes.hpp)
target_sources(FenrirSerialisationTests PRIVATE FromTree.cpp)
target_sources(FenrirSerialisationTests PRIVATE Resolvers.cpp)
target_sources(FenrirSerialisationTests PRIVATE Upgraders.cpp)
target_sources(FenrirSerialisationTests PRIVATE TreeBacked.cpp)
target_sources(FenrirSerialisationTests PRIVATE PriorTree.cpp)
target_sources(FenrirSerialisationTests PRIVATE ReadTree.cpp)
target_compile_definitions(FenrirSerialisationTests
                           PRIVATE FENRIR_SERIALISATION_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME FenrirSerialisation COMMAND FenrirSerialisationTests)

# The bridge over FenrirMath's real Vec3 and Quat, through their registered descriptions: a test executable of its own,
# so FenrirMath never reaches the one above, whose PublicHeaders.cpp proves the package's headers need nothing above it.
add_executable(FenrirSerialisationMathTests BridgeGoldens.cpp RealQuat.cpp GoldenTypes.hpp SerialisationTest.hpp)
target_sources(FenrirSerialisationMathTests PRIVATE PriorGoldens.cpp)
target_link_libraries(FenrirSerialisationMathTests PRIVATE FenrirSerialisation FenrirMath Catch2::Catch2WithMain)
target_compile_definitions(FenrirSerialisationMathTests
                           PRIVATE FENRIR_SERIALISATION_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME FenrirSerialisationMath COMMAND FenrirSerialisationMathTests)

# The README's save file example, compiled and run: its one C++ block is copied out of README.md when CMake
# configures, so an edit to the README rebuilds the test, and ReadmeExample.cpp includes the copy.
set(fenrir_serialisation_readme "${CMAKE_CURRENT_SOURCE_DIR}/../README.md")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${fenrir_serialisation_readme}")
file(READ "${fenrir_serialisation_readme}" fenrir_serialisation_readme_text)
string(REPLACE "\r\n" "\n" fenrir_serialisation_readme_text "${fenrir_serialisation_readme_text}")
string(FIND "${fenrir_serialisation_readme_text}" "\n```cpp\n" fenrir_serialisation_example_begin)
if(fenrir_serialisation_example_begin EQUAL -1)
    message(FATAL_ERROR "${fenrir_serialisation_readme} has no ```cpp block for ReadmeExample.cpp to compile")
endif()
math(EXPR fenrir_serialisation_example_begin "${fenrir_serialisation_example_begin} + 8")
string(SUBSTRING "${fenrir_serialisation_readme_text}" ${fenrir_serialisation_example_begin} -1
       fenrir_serialisation_example)
string(FIND "${fenrir_serialisation_example}" "\n```" fenrir_serialisation_example_end)
if(fenrir_serialisation_example_end EQUAL -1)
    message(FATAL_ERROR "${fenrir_serialisation_readme}'s ```cpp block is not closed")
endif()
string(SUBSTRING "${fenrir_serialisation_example}" ${fenrir_serialisation_example_end} -1 fenrir_serialisation_readme_rest)
string(SUBSTRING "${fenrir_serialisation_example}" 0 ${fenrir_serialisation_example_end} fenrir_serialisation_example)
# One block, copied as it is: a second would go untested, and file(CONFIGURE) would replace an @name@ in it.
string(FIND "${fenrir_serialisation_readme_rest}" "
```cpp
" fenrir_serialisation_second_example)
if(NOT fenrir_serialisation_second_example EQUAL -1)
    message(FATAL_ERROR "${fenrir_serialisation_readme} has a second ```cpp block, which ReadmeExample.cpp would not compile")
endif()
string(FIND "${fenrir_serialisation_example}" "@" fenrir_serialisation_example_at)
if(NOT fenrir_serialisation_example_at EQUAL -1)
    message(FATAL_ERROR "${fenrir_serialisation_readme}'s ```cpp block holds an @, which its copy would not keep")
endif()
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ReadmeExample/ReadmeExample.inl"
     CONTENT "@fenrir_serialisation_example@\n" @ONLY)
target_sources(FenrirSerialisationTests PRIVATE ReadmeExample.cpp)
target_include_directories(FenrirSerialisationTests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/ReadmeExample")
