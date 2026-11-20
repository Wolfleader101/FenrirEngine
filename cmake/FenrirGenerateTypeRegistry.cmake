# Type registrations (ADR-0074): tools/generate-type-registry reads a package's <Package>.api.json and the [registration]
# table of its <Package>.api.toml, and writes the committed header and source that table names: each described type's
# TypeDescription and Describe<T>, and the function adding them all to a TypeRegistry. The ordinary build never runs it:
# `cmake --build <dir> --target fenrir-generate-type-registry` rebuilds the tool and regenerates every registration from
# the committed descriptions (run fenrir-describe-api first when a header changed), and the package compiles the
# generated source and links FenrirTypes.
#
# fenrir_generate_type_registry(<target> <selection> [SKIP_UNSUPPORTED]) adds a package's registration to
# fenrir-generate-type-registry. The selection path is relative to the calling CMakeLists.txt; one without a
# [registration] table writes nothing. SKIP_UNSUPPORTED leaves out the types the tool cannot write yet (data assets,
# settings types and other types visited by member, which ticket 06 of the types spec adds, and tagged unions), with the
# types holding them, instead of failing on them.

# The same cached dotnet FenrirDescribeApi.cmake finds, when it is included first.
find_program(FENRIR_DOTNET_EXECUTABLE NAMES dotnet HINTS "$ENV{DOTNET_ROOT}"
    DOC "dotnet executable that builds and runs the .NET tools (describe-api, generate-type-registry)")

set(fenrir_generate_type_registry_project
    "${CMAKE_CURRENT_LIST_DIR}/../tools/generate-type-registry/src/GenerateTypeRegistry.csproj")
set(fenrir_generate_type_registry_tool "${CMAKE_BINARY_DIR}/generate-type-registry/tool/generate-type-registry.dll")

add_custom_target(fenrir-generate-type-registry)
if(FENRIR_DOTNET_EXECUTABLE)
    add_custom_target(fenrir-generate-type-registry-tool
        COMMAND "${FENRIR_DOTNET_EXECUTABLE}" build "${fenrir_generate_type_registry_project}" --configuration Release
            --output "${CMAKE_BINARY_DIR}/generate-type-registry/tool" --nologo -v:q
        COMMENT "Building tools/generate-type-registry"
        VERBATIM
    )
else()
    add_custom_target(fenrir-generate-type-registry-tool
        COMMAND "${CMAKE_COMMAND}" -E echo
            "fenrir-generate-type-registry needs the .NET SDK: install it or set FENRIR_DOTNET_EXECUTABLE."
        COMMAND "${CMAKE_COMMAND}" -E false
        VERBATIM
    )
endif()

function(fenrir_generate_type_registry target selection)
    cmake_parse_arguments(PARSE_ARGV 2 arg "SKIP_UNSUPPORTED" "" "")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "fenrir_generate_type_registry(${target}): unknown arguments ${arg_UNPARSED_ARGUMENTS}")
    endif()
    get_filename_component(selection "${selection}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT selection MATCHES "\\.api\\.toml$")
        message(FATAL_ERROR
            "fenrir_generate_type_registry(${target}): a selection is named <Package>.api.toml, not ${selection}")
    endif()
    set(skip "")
    if(arg_SKIP_UNSUPPORTED)
        set(skip --skip-unsupported)
    endif()

    add_custom_target(fenrir-generate-type-registry-${target}
        COMMAND "${FENRIR_DOTNET_EXECUTABLE}" "${fenrir_generate_type_registry_tool}" "${selection}" ${skip}
        COMMENT "Generating ${target}'s type registration"
        VERBATIM
    )
    add_dependencies(fenrir-generate-type-registry-${target} fenrir-generate-type-registry-tool)
    add_dependencies(fenrir-generate-type-registry fenrir-generate-type-registry-${target})
endfunction()
