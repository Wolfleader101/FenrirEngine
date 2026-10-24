# API descriptions (ADR-0042): tools/describe-api reads a package's selected headers and writes the committed
# <Package>.api.json beside its selection. The ordinary build never runs it: `cmake --build <dir> --target
# fenrir-describe-api` rebuilds the tool and regenerates every package's description, and CI fails on a dirty tree.
#
# fenrir_describe_api(<package-target> <selection>) adds a package to fenrir-describe-api. The selection path is relative
# to the calling CMakeLists.txt; the tool is given the target's include directories and definitions (its own and those
# of everything it links) and the project's C++ standard.
#
# fenrir_describe_api_natives(<target> <binding-config>) adds a binding's native functions to fenrir-describe-api: the
# tool's natives mode reads the C++ sources the configuration's [natives] section lists, compiled as <target> compiles
# them, and writes <Name>.natives.json beside the configuration.

find_program(FENRIR_DOTNET_EXECUTABLE NAMES dotnet HINTS "$ENV{DOTNET_ROOT}" DOC "dotnet executable that builds and runs tools/describe-api")

set(fenrir_describe_api_project "${CMAKE_CURRENT_LIST_DIR}/../tools/describe-api/src/DescribeApi.csproj")
set(fenrir_describe_api_tool "${CMAKE_BINARY_DIR}/describe-api/tool/describe-api.dll")

add_custom_target(fenrir-describe-api)
if(FENRIR_DOTNET_EXECUTABLE)
    add_custom_target(fenrir-describe-api-tool
        COMMAND "${FENRIR_DOTNET_EXECUTABLE}" build "${fenrir_describe_api_project}" --configuration Release
            --output "${CMAKE_BINARY_DIR}/describe-api/tool" --nologo -v:q
        COMMENT "Building tools/describe-api"
        VERBATIM
    )
else()
    add_custom_target(fenrir-describe-api-tool
        COMMAND "${CMAKE_COMMAND}" -E echo "fenrir-describe-api needs the .NET SDK: install it or set FENRIR_DOTNET_EXECUTABLE."
        COMMAND "${CMAKE_COMMAND}" -E false
        VERBATIM
    )
endif()

function(fenrir_describe_api target selection)
    get_filename_component(selection "${selection}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT selection MATCHES "\\.api\\.toml$")
        message(FATAL_ERROR "fenrir_describe_api(${target}): a selection is named <Package>.api.toml, not ${selection}")
    endif()
    fenrir_describe_api_arguments(${target} arguments_file)

    add_custom_target(fenrir-describe-api-${target}
        COMMAND "${FENRIR_DOTNET_EXECUTABLE}" "${fenrir_describe_api_tool}" "${selection}" "@${arguments_file}"
        COMMENT "Describing ${target}'s API"
        VERBATIM
    )
    add_dependencies(fenrir-describe-api-${target} fenrir-describe-api-tool)
    add_dependencies(fenrir-describe-api fenrir-describe-api-${target})
endfunction()

function(fenrir_describe_api_natives target config)
    get_filename_component(config "${config}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT config MATCHES "\\.binding\\.toml$")
        message(FATAL_ERROR
            "fenrir_describe_api_natives(${target}): a binding's configuration is named <Name>.binding.toml, not ${config}")
    endif()
    fenrir_describe_api_arguments(${target} arguments_file)

    add_custom_target(fenrir-describe-api-natives-${target}
        COMMAND "${FENRIR_DOTNET_EXECUTABLE}" "${fenrir_describe_api_tool}" --natives "${config}" "@${arguments_file}"
        COMMENT "Describing ${target}'s native functions"
        VERBATIM
    )
    add_dependencies(fenrir-describe-api-natives-${target} fenrir-describe-api-tool)
    add_dependencies(fenrir-describe-api fenrir-describe-api-natives-${target})
endfunction()

# Writes <target>'s compile arguments for the tool, one per line, read as @file; sets <out> to the file's path. A
# target's arguments are written once, however many times it is described.
function(fenrir_describe_api_arguments target out)
    set(arguments_file "${CMAKE_BINARY_DIR}/describe-api/${target}$<$<BOOL:$<CONFIG>>:-$<CONFIG>>.args")
    set(${out} "${arguments_file}" PARENT_SCOPE)
    get_property(written TARGET ${target} PROPERTY FENRIR_DESCRIBE_API_ARGUMENTS)
    if(written)
        return()
    endif()
    set_property(TARGET ${target} PROPERTY FENRIR_DESCRIBE_API_ARGUMENTS TRUE)

    # An interface library carries its usage requirements only.
    get_target_property(target_type ${target} TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY")
        set(prefix INTERFACE_)
    else()
        set(prefix "")
    endif()
    set(includes "$<TARGET_PROPERTY:${target},${prefix}INCLUDE_DIRECTORIES>")
    set(definitions "$<TARGET_PROPERTY:${target},${prefix}COMPILE_DEFINITIONS>")

    file(GENERATE OUTPUT "${arguments_file}" CONTENT
        "-std=c++${CMAKE_CXX_STANDARD}\n$<$<BOOL:${includes}>:-I$<JOIN:${includes},\n-I>\n>$<$<BOOL:${definitions}>:-D$<JOIN:${definitions},\n-D>\n>")
endfunction()
