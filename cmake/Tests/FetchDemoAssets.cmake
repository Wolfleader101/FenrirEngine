# One case of fenrir_fetch_demo_assets's tests, run by CTest in script mode:
#   cmake -DCASE=<case> -DMODULE=<FenrirDemoAssets.cmake> -DPROJECT=<FetchDemoAssetsProject> -DWORK=<dir>
#       -DGENERATOR=<CMake generator> -P FetchDemoAssets.cmake
# It writes an upstream file under WORK, a manifest pinning it by a file:// URL, and configures PROJECT against them.
# - WrongHashFailsConfigure: the manifest's SHA-256 is another file's; configure fails naming the file, and nothing is
#   left in the cache.
# - CachedFileIsNotDownloadedAgain: configure downloads the file once; with the upstream file gone, a second configure
#   in a clean build directory still passes, downloading nothing, from the cache.
foreach(required CASE MODULE PROJECT WORK GENERATOR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "FetchDemoAssets: ${required} is not set")
    endif()
endforeach()

# What the upstream file holds, and SHA-256s worked out apart from the module (sha256sum). No newline: file(WRITE)
# writes one as CRLF on Windows.
set(content "A fetched demo asset.")
set(content_sha256 "6bbd5350af57f5ed5e0614e57d31ce849007b9da9e73705fecef51d1f69c1a85")
# Of "Not the asset the manifest pinned.".
set(other_sha256 "6820603f9307425d8bf8d646c45ae1128a957abe236a0673b6b1325b388d1577")

file(REMOVE_RECURSE "${WORK}")
set(upstream "${WORK}/upstream/asset.txt")
file(WRITE "${upstream}" "${content}")
if(upstream MATCHES "^/")
    set(url "file://${upstream}")
else()
    set(url "file:///${upstream}")
endif()
set(cache "${WORK}/cache")
set(cached "${cache}/model/asset.txt")

# configure(<build dir> <sha256> <result var> <output var>): configures PROJECT with a manifest pinning the upstream
# file by <sha256>.
function(configure build sha256 result_var output_var)
    set(manifest "${WORK}/manifest.json")
    file(WRITE "${manifest}"
         "{\"assets\": [{\"path\": \"model/asset.txt\", \"url\": \"${url}\", \"sha256\": \"${sha256}\", "
         "\"licence\": \"CC0-1.0\"}]}\n")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${PROJECT}" -B "${build}" -G "${GENERATOR}" "-DMODULE=${MODULE}"
                "-DMANIFEST=${manifest}"
                "-DCACHE_DIR=${cache}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error_output
    )
    set(${result_var} "${result}" PARENT_SCOPE)
    set(${output_var} "${output}${error_output}" PARENT_SCOPE)
endfunction()

if(CASE STREQUAL "WrongHashFailsConfigure")
    configure("${WORK}/build" "${other_sha256}" result output)
    if(result EQUAL 0)
        message(FATAL_ERROR "Configure should fail on a file whose SHA-256 is not the manifest's, but passed:\n${output}")
    endif()
    # CMake wraps a long error message, so its words are matched with every run of whitespace made one space.
    string(REGEX REPLACE "[ \t\r\n]+" " " words "${output}")
    if(NOT words MATCHES "model/asset.txt from [^ ]+ has SHA-256 ${content_sha256}, but [^ ]+ expects ${other_sha256}")
        message(FATAL_ERROR "Configure failed, but without naming the file and both hashes:\n${output}")
    endif()
    if(EXISTS "${cached}" OR EXISTS "${cached}.part")
        message(FATAL_ERROR "Configure failed on the hash but left the download in the cache, under ${cache}")
    endif()
elseif(CASE STREQUAL "CachedFileIsNotDownloadedAgain")
    configure("${WORK}/build-first" "${content_sha256}" result output)
    if(NOT result EQUAL 0 OR NOT output MATCHES "1 files in .*, 1 downloaded")
        message(FATAL_ERROR "The first configure should download the one file:\n${output}")
    endif()
    file(READ "${cached}" got)
    if(NOT got STREQUAL content)
        message(FATAL_ERROR "The cached file holds '${got}', not the upstream file's '${content}'")
    endif()
    # A download now would fail: only the cache can satisfy the second configure.
    file(REMOVE "${upstream}")
    configure("${WORK}/build-second" "${content_sha256}" result output)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "With the file cached, a clean configure should not need the upstream file:\n${output}")
    endif()
    if(NOT output MATCHES "1 files in .*, 0 downloaded" OR output MATCHES "downloading model/asset.txt")
        message(FATAL_ERROR "With the file cached, a clean configure should download nothing:\n${output}")
    endif()
else()
    message(FATAL_ERROR "FetchDemoAssets: unknown CASE '${CASE}'")
endif()
message(STATUS "FetchDemoAssets ${CASE}: passed")
