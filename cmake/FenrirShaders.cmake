# fenrir_add_shaders: builds one GLSL shader program into a shader container with FenrirShaderCompiler (ADR-0018), for
# the engine's shaders and users' alike. The root CMakeLists.txt includes this file when FENRIR_ENABLE_RENDERER is on.
#
#   fenrir_add_shaders(<target>
#       PROGRAM <name>                  the program's name, a C++ identifier when EMBED is given
#       VERTEX <file> FRAGMENT <file>   its stages, GLSL #version 460 in the Vulkan dialect
#       [KEYS <key>...]                 its variant #define keys: every combination is built into the container
#       [INCLUDE_DIRECTORIES <dir>...]  searched for #include after the stage's own directory, before the engine's
#       [OUTPUT_DIRECTORY <dir>]        where <name>.fshader goes (default: <current binary dir>/shaders)
#       [EMBED])                        also generate <FenrirShaders/<name>.hpp>, the container as a byte array
#
# <target> depends on the container, so building it builds the shaders. #include resolves against the stage's own
# directory, then INCLUDE_DIRECTORIES, then the engine's shared include directory (FENRIR_SHADER_INCLUDE_DIR, holding
# Fenrir/Frame.glsl and the rest). The tool writes a depfile, so editing any included file rebuilds the containers that
# include it. With EMBED, <target> gets the generated header's directory as a PRIVATE include directory; the header
# declares `Fenrir::Shaders::<name>`, an `inline constexpr std::uint8_t[]` holding the container's bytes.
include_guard(GLOBAL)

set(FENRIR_SHADER_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../packages/FenrirRenderer/Shaders/include")
cmake_path(NORMAL_PATH FENRIR_SHADER_INCLUDE_DIR)

function(fenrir_add_shaders target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "EMBED" "PROGRAM;VERTEX;FRAGMENT;OUTPUT_DIRECTORY"
                          "KEYS;INCLUDE_DIRECTORIES")
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "fenrir_add_shaders(${target}): unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()
    foreach(required PROGRAM VERTEX FRAGMENT)
        if(NOT ARG_${required})
            message(FATAL_ERROR "fenrir_add_shaders(${target}): ${required} is required")
        endif()
    endforeach()
    if(NOT ARG_OUTPUT_DIRECTORY)
        set(ARG_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    endif()

    cmake_path(ABSOLUTE_PATH ARG_VERTEX BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT_VARIABLE vertex)
    cmake_path(ABSOLUTE_PATH ARG_FRAGMENT BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT_VARIABLE fragment)
    cmake_path(ABSOLUTE_PATH ARG_OUTPUT_DIRECTORY BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" OUTPUT_VARIABLE out_dir)

    set(container "${out_dir}/${ARG_PROGRAM}.fshader")
    set(depfile "${CMAKE_CURRENT_BINARY_DIR}/shaders/${ARG_PROGRAM}.fshader.d")
    set(outputs "${container}")
    set(arguments --name "${ARG_PROGRAM}" --vertex "${vertex}" --fragment "${fragment}" --output "${container}"
                  --depfile "${depfile}")
    foreach(key IN LISTS ARG_KEYS)
        list(APPEND arguments --key "${key}")
    endforeach()
    foreach(dir IN LISTS ARG_INCLUDE_DIRECTORIES)
        cmake_path(ABSOLUTE_PATH dir BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT_VARIABLE dir)
        list(APPEND arguments --include "${dir}")
    endforeach()
    list(APPEND arguments --include "${FENRIR_SHADER_INCLUDE_DIR}")

    if(ARG_EMBED)
        set(header_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders/include")
        set(header "${header_dir}/FenrirShaders/${ARG_PROGRAM}.hpp")
        list(APPEND outputs "${header}")
        list(APPEND arguments --header "${header}")
        target_include_directories(${target} PRIVATE "${header_dir}")
    endif()

    add_custom_command(
        OUTPUT ${outputs}
        COMMAND FenrirShaderCompiler ${arguments}
        DEPENDS FenrirShaderCompiler "${vertex}" "${fragment}"
        DEPFILE "${depfile}"
        COMMENT "Building shader program ${ARG_PROGRAM}"
        VERBATIM
    )
    target_sources(${target} PRIVATE ${outputs})
    set_source_files_properties(${outputs} PROPERTIES GENERATED TRUE HEADER_FILE_ONLY TRUE)
endfunction()
