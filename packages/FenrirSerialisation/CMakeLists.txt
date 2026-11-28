# FenrirSerialisation: the value tree, its JSON5 text, its tree encoding and the bridge to described values (ADR-0077,
# ADR-0079, ADR-0083). It knows no files, assets or scenes, has no libs/ directory and links FenrirTypes and
# FenrirLogger alone (ADR-0079 §1, ADR-0010).
add_library(FenrirSerialisation STATIC
    src/Value.cpp
    src/Text.cpp
    src/ReadText.cpp
    src/ToTree.cpp
    src/FromTree.cpp
    src/Encoding.cpp
    src/Upgrade.cpp
    src/ReadTree.cpp
    include/FenrirSerialisation/Bridge.hpp
    include/FenrirSerialisation/Diagnostic.hpp
    include/FenrirSerialisation/Encoding.hpp
    include/FenrirSerialisation/HeldFields.hpp
    include/FenrirSerialisation/ReadTree.hpp
    include/FenrirSerialisation/Text.hpp
    include/FenrirSerialisation/Upgrade.hpp
    include/FenrirSerialisation/Value.hpp
)

target_include_directories(FenrirSerialisation PUBLIC include)
target_link_libraries(FenrirSerialisation PUBLIC FenrirTypes FenrirLogger)

# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON (see the root CMakeLists.txt).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
