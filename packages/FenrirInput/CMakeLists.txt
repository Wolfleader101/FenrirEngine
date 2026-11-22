# FenrirInput: device-neutral input types, the Keyboard, Mouse and Gamepads app resources, input events, the input
# feed (ADR-0017), and input routers with their control owners and input claims (ADR-0027). Links no third-party
# library and is always built, like FenrirTime.
# It registers its described types with FenrirTypes' type registry (ADR-0074), through the generated registration
# it compiles, so it links FenrirTypes PUBLIC: the generated header, public, gives a game Describe<T>.
add_library(FenrirInput STATIC
    src/Devices.cpp
    src/Input.cpp
    src/InputDispatch.hpp
    src/InputFeed.cpp
    src/InputRouting.cpp
    include/FenrirInput/Devices.hpp
    include/FenrirInput/Input.hpp
    include/FenrirInput/InputEvents.hpp
    include/FenrirInput/InputFeed.hpp
    include/FenrirInput/InputRouter.hpp
    include/FenrirInput/InputTypes.hpp
)

target_link_libraries(FenrirInput PUBLIC FenrirApp FenrirMath)

target_include_directories(FenrirInput PUBLIC include)

# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON (see the root CMakeLists.txt).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

# Its shared types (ADR-0042): `cmake --build <dir> --target fenrir-describe-api` regenerates api/FenrirInput.api.json,
# which FenrirInputScripting's binding binds.
fenrir_describe_api(FenrirInput api/FenrirInput.api.toml)
# Its type registration (ADR-0074), from that description: `cmake --build <dir> --target
# fenrir-generate-type-registry` regenerates include/FenrirInput/Generated/ and src/Generated/.
fenrir_generate_type_registry(FenrirInput api/FenrirInput.api.toml)
target_sources(FenrirInput PRIVATE include/FenrirInput/Generated/FenrirInputTypes.hpp
                                   src/Generated/FenrirInputTypes.cpp)
target_link_libraries(FenrirInput PUBLIC FenrirTypes)
