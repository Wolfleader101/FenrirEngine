# fenrir_link_demo_assets's step, run by CTest in script mode:
#   cmake -DMODULE=<FenrirDemoAssets.cmake> -DWORK=<dir> -P LinkDemoAssets.cmake
# It writes a cache holding one of a manifest's two files and links them into an asset root under WORK: the cached file
# appears there with its content, the missing one is skipped, a second link leaves it as it is, and a new file in the
# cache replaces it.
foreach(required MODULE WORK)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "LinkDemoAssets: ${required} is not set")
    endif()
endforeach()
include("${MODULE}")

file(REMOVE_RECURSE "${WORK}")
set(cache "${WORK}/cache")
set(root "${WORK}/assets/fetched")
set(manifest "${WORK}/manifest.json")
file(WRITE "${manifest}"
     "{\"assets\": [{\"path\": \"model/asset.txt\", \"url\": \"unused\", \"sha256\": \"unused\", \"licence\": \"CC0-1.0\"},"
     " {\"path\": \"model/missing.txt\", \"url\": \"unused\", \"sha256\": \"unused\", \"licence\": \"CC0-1.0\"}]}\n")
# No newlines: file(WRITE) writes one as CRLF on Windows.
file(WRITE "${cache}/model/asset.txt" "A fetched demo asset.")

fenrir_link_fetched_files("${manifest}" "${cache}" "${root}")
file(READ "${root}/model/asset.txt" got)
if(NOT got STREQUAL "A fetched demo asset.")
    message(FATAL_ERROR "The linked file holds '${got}', not the cached file's content")
endif()
if(EXISTS "${root}/model/missing.txt")
    message(FATAL_ERROR "A file the cache lacks was put in the asset root")
endif()

# Linking again finds the file in place and leaves it.
fenrir_link_fetched_files("${manifest}" "${cache}" "${root}")
file(READ "${root}/model/asset.txt" again)
if(NOT again STREQUAL "A fetched demo asset.")
    message(FATAL_ERROR "A second link changed the file to '${again}'")
endif()

# Configure replaces a cached file by renaming a new one over it, which the next link picks up. It is written a second
# after the first, so the test does not depend on the file system's time resolution.
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1.1)
file(WRITE "${cache}/model/asset.txt.part" "A newer fetched demo asset.")
file(RENAME "${cache}/model/asset.txt.part" "${cache}/model/asset.txt")
fenrir_link_fetched_files("${manifest}" "${cache}" "${root}")
file(READ "${root}/model/asset.txt" replaced)
if(NOT replaced STREQUAL "A newer fetched demo asset.")
    message(FATAL_ERROR "After the cache got a new file, the asset root still holds '${replaced}'")
endif()
message(STATUS "LinkDemoAssets: passed")
