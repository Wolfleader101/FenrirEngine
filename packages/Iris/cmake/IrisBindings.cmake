# Bindings from API descriptions (Tools/Iris.Bindings): the tool turns descriptions and a binding's configuration into
# committed C# mirrors, Iris mappings, registration lists and a defaults test. The ordinary build never runs it:
# `cmake --build <dir> --target <IRIS_GENERATE_BINDINGS_TARGET>` rebuilds the tool and regenerates every binding.
#
# iris_generate_binding(<name> <config> <descriptions...>) adds a binding to that target. Paths are relative to the
# calling CMakeLists.txt. A parent project names the target by setting IRIS_GENERATE_BINDINGS_TARGET before adding Iris
# (iris-generate-bindings when it does not).
#
# iris_binding_outputs(<target>...) lets a new binding configure before its first generation: call it after the targets
# that compile the binding's generated sources, and each of their sources under a Generated/ directory that does not exist
# yet is created empty, for the bindings target to fill in.

if(NOT IRIS_GENERATE_BINDINGS_TARGET)
    set(IRIS_GENERATE_BINDINGS_TARGET iris-generate-bindings)
endif()
if(NOT IRIS_DOTNET_EXECUTABLE)
    find_program(IRIS_DOTNET_EXECUTABLE NAMES dotnet HINTS "$ENV{DOTNET_ROOT}"
        DOC "dotnet executable used to build the managed bridge and the bindings tool")
endif()

set(iris_bindings_tool "${CMAKE_BINARY_DIR}/iris-bindings/tool/iris-bindings.dll")
# The function runs in its callers' scopes, so what it needs is kept where every directory sees it.
set_property(GLOBAL PROPERTY IRIS_GENERATE_BINDINGS_TARGET "${IRIS_GENERATE_BINDINGS_TARGET}")
set_property(GLOBAL PROPERTY IRIS_BINDINGS_TOOL "${iris_bindings_tool}")

add_custom_target(${IRIS_GENERATE_BINDINGS_TARGET})
if(IRIS_DOTNET_EXECUTABLE)
    add_custom_target(iris-bindings-tool
        COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_LIST_DIR}/../Tools/Iris.Bindings/src/Iris.Bindings.csproj"
            --configuration Release --output "${CMAKE_BINARY_DIR}/iris-bindings/tool" --nologo -v:q
        COMMENT "Building Iris.Bindings"
        VERBATIM
    )
else()
    add_custom_target(iris-bindings-tool
        COMMAND "${CMAKE_COMMAND}" -E echo "Generating bindings needs the .NET SDK: install it or set IRIS_DOTNET_EXECUTABLE."
        COMMAND "${CMAKE_COMMAND}" -E false
        VERBATIM
    )
endif()

function(iris_generate_binding name config)
    get_filename_component(config "${config}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT config MATCHES "\\.binding\\.toml$")
        message(FATAL_ERROR "iris_generate_binding(${name}): a binding's configuration is named <Name>.binding.toml, not ${config}")
    endif()
    if(ARGC LESS 3)
        message(FATAL_ERROR "iris_generate_binding(${name}): name at least one <Package>.api.json")
    endif()
    set(descriptions "")
    foreach(description IN LISTS ARGN)
        get_filename_component(description "${description}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        list(APPEND descriptions "${description}")
    endforeach()

    get_property(target GLOBAL PROPERTY IRIS_GENERATE_BINDINGS_TARGET)
    get_property(tool GLOBAL PROPERTY IRIS_BINDINGS_TOOL)
    add_custom_target(iris-generate-binding-${name}
        COMMAND "${IRIS_DOTNET_EXECUTABLE}" "${tool}" "${config}" ${descriptions}
        COMMENT "Generating the ${name} binding"
        VERBATIM
    )
    add_dependencies(iris-generate-binding-${name} iris-bindings-tool)
    add_dependencies(${target} iris-generate-binding-${name})
endfunction()

function(iris_binding_outputs)
    foreach(target IN LISTS ARGN)
        get_target_property(sources ${target} SOURCES)
        get_target_property(directory ${target} SOURCE_DIR)
        foreach(source IN LISTS sources)
            # Generator expressions name nothing on disk yet.
            if(source MATCHES "(^|/)Generated/" AND NOT source MATCHES "\\$<")
                get_filename_component(path "${source}" ABSOLUTE BASE_DIR "${directory}")
                if(NOT EXISTS "${path}")
                    message(STATUS "${path} is not generated yet: it is empty until the bindings target writes it")
                    file(WRITE "${path}" "")
                endif()
            endif()
        endforeach()
    endforeach()
endfunction()
