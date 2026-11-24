# FenrirApp: the App, its frame, phases, systems, scenes, events and app resources, the type registry among them.
# It registers its described types with FenrirTypes' type registry (ADR-0074), through the generated registration
# it compiles, so it links FenrirTypes PUBLIC: the generated header, public, gives a game Describe<T>.
add_library(FenrirApp STATIC
    src/App.cpp
    include/FenrirApp/App.hpp
)
target_compile_features(FenrirApp PUBLIC cxx_std_20)

# link against other interal libraries
target_link_libraries(FenrirApp PUBLIC FenrirScheduler FenrirTime FenrirLogger FenrirScene FenrirECS)

target_include_directories(FenrirApp PUBLIC include)
# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON (see the root CMakeLists.txt).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

# Its shared types (ADR-0042): `cmake --build <dir> --target fenrir-describe-api` regenerates api/FenrirApp.api.json.
fenrir_describe_api(FenrirApp api/FenrirApp.api.toml)
# Its type registration (ADR-0074), from that description: `cmake --build <dir> --target
# fenrir-generate-type-registry` regenerates include/FenrirApp/Generated/ and src/Generated/.
fenrir_generate_type_registry(FenrirApp api/FenrirApp.api.toml)
target_sources(FenrirApp PRIVATE include/FenrirApp/Generated/FenrirAppTypes.hpp
                                 src/Generated/FenrirAppTypes.cpp)
target_link_libraries(FenrirApp PUBLIC FenrirTypes)
