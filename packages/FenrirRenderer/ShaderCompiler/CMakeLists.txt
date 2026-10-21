# SPIKE (prototype/fenrir-renderer-spike, throwaway): FenrirShaderCompiler, the build-time tool of ADR-0018.
add_subdirectory(libs)

add_executable(FenrirShaderCompiler src/main.cpp)
target_link_libraries(FenrirShaderCompiler PRIVATE glslang glslang-default-resource-limits SpirvReflect)
target_include_directories(FenrirShaderCompiler SYSTEM PRIVATE libs/glslang)
# The tool is third-party glue, not engine code: keep -Weverything off it.
target_compile_options(FenrirShaderCompiler PRIVATE $<$<CXX_COMPILER_ID:Clang>:-w>)

set(FENRIR_SHADER_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../shaders/include" CACHE INTERNAL "")

# fenrir_add_shaders(<target> NAME <Name> VERT <file> FRAG <file>): compiles one program into
# <binary>/shaders/<Name>.hpp (namespace Fenrir::Shaders, array <Name>) and adds it to <target>.
function(fenrir_add_shaders target)
    cmake_parse_arguments(ARG "" "NAME;VERT;FRAG" "" ${ARGN})
    set(out "${CMAKE_CURRENT_BINARY_DIR}/shaders/${ARG_NAME}.hpp")
    get_filename_component(vert "${ARG_VERT}" ABSOLUTE)
    get_filename_component(frag "${ARG_FRAG}" ABSOLUTE)
    file(GLOB includes CONFIGURE_DEPENDS "${FENRIR_SHADER_INCLUDE_DIR}/Fenrir/*.glsl")
    add_custom_command(
        OUTPUT "${out}"
        COMMAND FenrirShaderCompiler --name ${ARG_NAME} --out "${out}" -I "${FENRIR_SHADER_INCLUDE_DIR}"
                --vert "${vert}" --frag "${frag}"
        DEPENDS FenrirShaderCompiler "${vert}" "${frag}" ${includes}
        COMMENT "Compiling shader program ${ARG_NAME}"
        VERBATIM)
    target_sources(${target} PRIVATE "${out}")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/shaders")
endfunction()
