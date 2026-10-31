# Tests of the build's own CMake modules, each a CMake script run by CTest.

# fenrir_fetch_demo_assets (cmake/FenrirDemoAssets.cmake), configuring a small project against file:// URLs, so no test
# downloads anything (ADR-0052).
foreach(case IN ITEMS WrongHashFailsConfigure CachedFileIsNotDownloadedAgain)
    add_test(NAME FetchDemoAssets-${case}
             COMMAND "${CMAKE_COMMAND}" "-DCASE=${case}" "-DMODULE=${PROJECT_SOURCE_DIR}/cmake/FenrirDemoAssets.cmake"
                     "-DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/FetchDemoAssetsProject"
                     "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/FetchDemoAssets/${case}" "-DGENERATOR=${CMAKE_GENERATOR}" -P
                     "${CMAKE_CURRENT_SOURCE_DIR}/FetchDemoAssets.cmake")
endforeach()
