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

# fenrir_link_demo_assets's step (fenrir_link_fetched_files), run in script mode on a cache and an asset root under WORK.
add_test(NAME LinkDemoAssets
         COMMAND "${CMAKE_COMMAND}" "-DMODULE=${PROJECT_SOURCE_DIR}/cmake/FenrirDemoAssets.cmake"
                 "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/LinkDemoAssets" -P "${CMAKE_CURRENT_SOURCE_DIR}/LinkDemoAssets.cmake")
