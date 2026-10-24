# Fails if the scripting package's code uses EnTT (an `entt::` name or an `entt/` include; prose that names the library is fine). The ECS is wrapped by FenrirECS so that it can change (another library,
# or one written in house) without breaking code built on it; FenrirScripting must use only the wrapper. Run by CTest:
#   cmake -DSOURCE_DIR=<packages/FenrirScripting> -P NoEnTT.cmake
if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "NoEnTT: SOURCE_DIR is not set")
endif()
file(GLOB_RECURSE files
    "${SOURCE_DIR}/*.cpp" "${SOURCE_DIR}/*.hpp" "${SOURCE_DIR}/*.h" "${SOURCE_DIR}/*.cs" "${SOURCE_DIR}/*.csproj"
    "${SOURCE_DIR}/*.props" "${SOURCE_DIR}/CMakeLists.txt")
set(offenders "")
foreach(file IN LISTS files)
    # Build output (obj/, bin/) and this checker itself are not source.
    if(file MATCHES "/(obj|bin)/")
        continue()
    endif()
    file(STRINGS "${file}" lines REGEX "entt::|entt/|<entt|\"entt|namespace entt|ENTT_|EnTT::")
    if(lines)
        string(APPEND offenders "  ${file}\n")
    endif()
endforeach()
if(offenders)
    message(FATAL_ERROR "FenrirScripting code uses EnTT; use the FenrirECS API instead:\n${offenders}")
endif()
message(STATUS "No EnTT in FenrirScripting.")
