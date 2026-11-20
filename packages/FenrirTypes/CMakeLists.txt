# FenrirTypes: the described kinds, type descriptions, the field visitor and the type registry (ADR-0074). It knows no
# file format and no language, and links nothing but the standard library.
add_library(FenrirTypes STATIC
    src/Kind.cpp
    src/TypeDescription.cpp
    src/TypeRegistry.cpp
    src/ValueRef.cpp
    include/FenrirTypes/Describe.hpp
    include/FenrirTypes/FieldInfo.hpp
    include/FenrirTypes/FieldVisitor.hpp
    include/FenrirTypes/Kind.hpp
    include/FenrirTypes/RoleHooks.hpp
    include/FenrirTypes/TypeDescription.hpp
    include/FenrirTypes/TypeRegistry.hpp
    include/FenrirTypes/ValueRef.hpp
)

target_include_directories(FenrirTypes PUBLIC include)

# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON (see the root CMakeLists.txt).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
