# FenrirAssets' data asset test package: a header no engine package owns, described by fenrir-describe-api and registered
# by fenrir-generate-type-registry as a game's own types are, so both tools run on it for real and CI's dirty-tree check
# covers its committed DataAssetsTestPackage.api.json and registration. Added whether or not the tests build, since the
# describe step configures without them; FenrirAssets' tests compile the registration (../CMakeLists.txt).
add_library(FenrirAssetsDataTestPackage INTERFACE)
target_include_directories(FenrirAssetsDataTestPackage INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(FenrirAssetsDataTestPackage INTERFACE FenrirAssets)
fenrir_describe_api(FenrirAssetsDataTestPackage DataAssetsTestPackage.api.toml)
fenrir_generate_type_registry(FenrirAssetsDataTestPackage DataAssetsTestPackage.api.toml)
