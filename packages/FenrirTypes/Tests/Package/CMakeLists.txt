# The FenrirTypes test package: a header no engine package owns, described by fenrir-describe-api and registered by
# fenrir-generate-type-registry like a package, so both tools run on it for real and CI's dirty-tree check covers its
# committed TestPackage.api.json (and, from ticket 10 of the types spec, its registration). Added whether or not the tests
# build, since the describe step configures without them; FenrirTypes' tests compile the registration
# (../CMakeLists.txt).
add_library(FenrirTypesTestPackage INTERFACE)
target_include_directories(FenrirTypesTestPackage INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
fenrir_describe_api(FenrirTypesTestPackage TestPackage.api.toml)
fenrir_generate_type_registry(FenrirTypesTestPackage TestPackage.api.toml)
