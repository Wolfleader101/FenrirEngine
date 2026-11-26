# move-asset (ADR-0079 §6): Assets::Move from a shell. FenrirMoveAsset is the whole tool behind MoveAssetMain, which a
# game's own copy calls with its registration (README.md); fenrir-move-asset is the engine's copy, with the engine's
# registrations alone. Added with FenrirAssets, after it.
add_library(FenrirMoveAsset STATIC include/FenrirMoveAsset/MoveAssetMain.hpp src/MoveAssetMain.cpp)
target_include_directories(FenrirMoveAsset PUBLIC include)
target_link_libraries(FenrirMoveAsset PRIVATE FenrirAssets)

add_executable(fenrir-move-asset src/main.cpp)
target_link_libraries(fenrir-move-asset PRIVATE FenrirMoveAsset)

# The smoke run: a copy of the tool built as a game builds its own, with FenrirAssets' data asset test package's
# registration, run by tests/SmokeRun.cmake on a copy of tests/root in the build tree.
if(FENRIR_BUILD_TESTS)
    set(test_package "${CMAKE_CURRENT_SOURCE_DIR}/../../packages/FenrirAssets/Tests/Package")
    add_executable(fenrir-move-asset-test-package tests/TestPackageMain.cpp
                   "${test_package}/Generated/DataAssetsTestPackageTypes.hpp"
                   "${test_package}/Generated/DataAssetsTestPackageTypes.cpp")
    target_link_libraries(fenrir-move-asset-test-package PRIVATE FenrirMoveAsset FenrirAssetsDataTestPackage)

    add_test(NAME MoveAssetSmoke
        COMMAND "${CMAKE_COMMAND}" "-DGAME_TOOL=$<TARGET_FILE:fenrir-move-asset-test-package>"
            "-DENGINE_TOOL=$<TARGET_FILE:fenrir-move-asset>" "-DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/tests/root"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/smoke-root" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/SmokeRun.cmake")
endif()
