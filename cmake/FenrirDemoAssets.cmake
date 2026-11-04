# fenrir_fetch_demo_assets(MANIFEST <file> CACHE_DIR <dir>)
#
# Downloads a demo app's fetched assets at configure time (ADR-0052), for FENRIR_FETCH_DEMO_ASSETS. MANIFEST is a JSON
# file, {"assets": [{"path", "url", "sha256", "licence"}, ...]}: each file's place in the cache, its pinned upstream URL,
# its SHA-256 and its SPDX licence expression. Each file is kept at CACHE_DIR/<path>, outside the build directory, so a
# clean build does not download it again: a cached file whose SHA-256 matches is used as it is, and any other is
# downloaded again. A download that fails, or whose SHA-256 is not the manifest's, fails configure naming the file, and
# leaves nothing in the cache. Editing the manifest re-runs configure.
include_guard(GLOBAL)

function(fenrir_fetch_demo_assets)
    cmake_parse_arguments(PARSE_ARGV 0 fetch "" "MANIFEST;CACHE_DIR" "")
    if(NOT fetch_MANIFEST OR NOT fetch_CACHE_DIR)
        message(FATAL_ERROR "fenrir_fetch_demo_assets: MANIFEST and CACHE_DIR are required")
    endif()
    cmake_path(ABSOLUTE_PATH fetch_MANIFEST BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${fetch_MANIFEST}")

    file(READ "${fetch_MANIFEST}" manifest)
    string(JSON count LENGTH "${manifest}" assets)
    set(downloaded 0)
    if(count GREATER 0)
        math(EXPR last "${count} - 1")
        foreach(index RANGE ${last})
            foreach(field IN ITEMS path url sha256 licence)
                string(JSON ${field} ERROR_VARIABLE missing GET "${manifest}" assets ${index} ${field})
                if(missing)
                    message(FATAL_ERROR "FENRIR_FETCH_DEMO_ASSETS: entry ${index} of ${fetch_MANIFEST} has no "
                                        "'${field}'")
                endif()
            endforeach()
            string(TOLOWER "${sha256}" expected)
            set(file "${fetch_CACHE_DIR}/${path}")

            if(EXISTS "${file}")
                file(SHA256 "${file}" cached)
                if(cached STREQUAL expected)
                    continue()
                endif()
                message(STATUS "FENRIR_FETCH_DEMO_ASSETS: the cached ${path} has changed; downloading it again")
            endif()

            message(STATUS "FENRIR_FETCH_DEMO_ASSETS: downloading ${path} (${licence})")
            # Into a part file beside it, so an interrupted or wrong download never stands in the cache as the file.
            set(part "${file}.part")
            file(DOWNLOAD "${url}" "${part}" STATUS status TLS_VERIFY ON)
            list(GET status 0 code)
            if(NOT code EQUAL 0)
                list(GET status 1 reason)
                file(REMOVE "${part}")
                message(FATAL_ERROR "FENRIR_FETCH_DEMO_ASSETS: ${path} did not download from ${url}: ${reason}")
            endif()
            file(SHA256 "${part}" actual)
            if(NOT actual STREQUAL expected)
                file(REMOVE "${part}")
                message(FATAL_ERROR "FENRIR_FETCH_DEMO_ASSETS: ${path} from ${url} has SHA-256 ${actual}, but "
                                    "${fetch_MANIFEST} expects ${expected}")
            endif()
            file(RENAME "${part}" "${file}")
            math(EXPR downloaded "${downloaded} + 1")
        endforeach()
    endif()
    message(STATUS "FENRIR_FETCH_DEMO_ASSETS: ${count} files in ${fetch_CACHE_DIR}, ${downloaded} downloaded")
endfunction()
