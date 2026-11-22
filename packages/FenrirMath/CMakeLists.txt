# FenrirMath: the engine's colour, vectors, quaternion, matrix, ray, box and frustum, over glm.
# It registers its described types with FenrirTypes' type registry (ADR-0074), through the generated registration
# it compiles, so it links FenrirTypes PUBLIC: the generated header, public, gives a game Describe<T>.
add_library(FenrirMath STATIC
    src/Frustum.cpp
    src/Math.cpp
    include/FenrirMath/Colour.hpp
    include/FenrirMath/Frustum.hpp
    include/FenrirMath/Math.hpp
    include/FenrirMath/Math_fwd.hpp
)

add_subdirectory(libs)

target_link_libraries(FenrirMath PUBLIC glm)

target_include_directories(FenrirMath PUBLIC include)

# Its shared types (ADR-0042): `cmake --build <dir> --target fenrir-describe-api` regenerates api/FenrirMath.api.json.
fenrir_describe_api(FenrirMath api/FenrirMath.api.toml)
# Its type registration (ADR-0074), from that description: `cmake --build <dir> --target
# fenrir-generate-type-registry` regenerates include/FenrirMath/Generated/ and src/Generated/.
fenrir_generate_type_registry(FenrirMath api/FenrirMath.api.toml)
target_sources(FenrirMath PRIVATE include/FenrirMath/Generated/FenrirMathTypes.hpp
                                  src/Generated/FenrirMathTypes.cpp)
target_link_libraries(FenrirMath PUBLIC FenrirTypes)

# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON (see the root CMakeLists.txt).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
