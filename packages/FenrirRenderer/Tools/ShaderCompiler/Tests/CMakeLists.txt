# FenrirShaderCompiler's tests (the spec's seam 5). The fixture programs in fixtures/ are built by fenrir_add_shaders,
# as any engine or user shader is, and embedded in the test executable, which reads them back through
# FenrirShaderContainer. The fixtures in fixtures/errors/ must fail to build: each is a CTest case that runs the tool
# and checks it fails with the expected message naming the file and line.
include("${CMAKE_CURRENT_LIST_DIR}/../../../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirShaderCompilerTests PublicHeaders.cpp ShaderCompilerTest.hpp Flattening.cpp Variants.cpp
                                         MaterialReflection.cpp Containers.cpp)
target_link_libraries(FenrirShaderCompilerTests PRIVATE FenrirShaderContainer Catch2::Catch2WithMain)

set(fixtures "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
fenrir_add_shaders(FenrirShaderCompilerTests PROGRAM Flatten VERTEX fixtures/flatten.vert
                   FRAGMENT fixtures/flatten.frag EMBED)
fenrir_add_shaders(FenrirShaderCompilerTests PROGRAM Includes VERTEX fixtures/plain.vert
                   FRAGMENT fixtures/includes.frag INCLUDE_DIRECTORIES fixtures/include EMBED)
fenrir_add_shaders(FenrirShaderCompilerTests PROGRAM Variants VERTEX fixtures/plain.vert
                   FRAGMENT fixtures/variants.frag KEYS FIRST SECOND EMBED)
fenrir_add_shaders(FenrirShaderCompilerTests PROGRAM Material VERTEX fixtures/uv.vert
                   FRAGMENT fixtures/material.frag EMBED)
fenrir_add_shaders(FenrirShaderCompilerTests PROGRAM TwoD VERTEX fixtures/twod.vert FRAGMENT fixtures/twod.frag EMBED)
# The depfile fenrir_add_shaders has the tool write, which the Includes test reads.
target_compile_definitions(FenrirShaderCompilerTests PRIVATE
    FENRIR_SHADER_TEST_DEPFILE="${CMAKE_CURRENT_BINARY_DIR}/shaders/Includes.fshader.d"
    FENRIR_SHADER_TEST_FIXTURES="${fixtures}"
    FENRIR_SHADER_TEST_ENGINE_INCLUDE="${FENRIR_SHADER_INCLUDE_DIR}")

add_test(NAME FenrirShaderCompiler COMMAND FenrirShaderCompilerTests)

# fenrir_shader_build_fails(<case> <vertex> <fragment> <expected regex>): the tool, run on the two stages, must fail,
# print a message matching the regex, and write no container.
function(fenrir_shader_build_fails case vertex fragment expected)
    add_test(NAME FenrirShaderCompiler-Fails-${case}
             COMMAND "${CMAKE_COMMAND}" "-DTOOL=$<TARGET_FILE:FenrirShaderCompiler>" "-DVERTEX=${fixtures}/${vertex}"
                     "-DFRAGMENT=${fixtures}/${fragment}" "-DINCLUDE=${FENRIR_SHADER_INCLUDE_DIR}"
                     "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/failing/${case}" "-DEXPECT=${expected}"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/ExpectBuildFailure.cmake")
endfunction()

fenrir_shader_build_fails(InstanceIndex errors/instance_index.vert plain.frag
    "errors/instance_index.vert:6: error: gl_InstanceIndex is refused")
fenrir_shader_build_fails(NoSet plain.vert errors/no_set.frag "errors/no_set.frag:9: error: 'picture' has no layout[(]set")
fenrir_shader_build_fails(LooseUniform plain.vert errors/loose_uniform.frag
    "errors/loose_uniform.frag:4: 'non-opaque uniforms outside a block'")
fenrir_shader_build_fails(NoLocation plain.vert errors/no_location.frag
    "errors/no_location.frag:4: 'location' : SPIR-V requires location")
fenrir_shader_build_fails(FiveTexturesInASet plain.vert errors/five_textures.frag
    "errors/five_textures.frag: error: 'fifth' is texture 5 of set 2, but a set holds at most 4 bindings of a kind")
fenrir_shader_build_fails(PragmaUnknownMember plain.vert errors/pragma_unknown.frag
    "errors/pragma_unknown.frag:8: error: unknown member: 'tnit'")
fenrir_shader_build_fails(PragmaMalformed plain.vert errors/pragma_malformed.frag
    "errors/pragma_malformed.frag:8: error: malformed pragma")
fenrir_shader_build_fails(PragmaWrongCount plain.vert errors/pragma_count.frag
    "errors/pragma_count.frag:8: error: wrong value count: 'tint' is a vec4 and takes 4 values, but the pragma gives 3")
fenrir_shader_build_fails(PragmaTwiceInAStage plain.vert errors/pragma_twice.frag
    "errors/pragma_twice.frag:9: error: a second default for 'strength' in the fragment stage")
fenrir_shader_build_fails(PragmaStagesDisagree errors/pragma_disagree.vert errors/pragma_disagree.frag
    "errors/pragma_disagree.frag:8: error: the stages disagree on the default of 'strength'")
fenrir_shader_build_fails(PragmaOnArray plain.vert errors/pragma_array.frag
    "errors/pragma_array.frag:8: error: 'weights' is an array member")
fenrir_shader_build_fails(PragmaOnStruct plain.vert errors/pragma_struct.frag
    "errors/pragma_struct.frag:13: error: 'pair' is a struct member")
fenrir_shader_build_fails(PragmaOnMatrix plain.vert errors/pragma_matrix.frag
    "errors/pragma_matrix.frag:8: error: 'turn' is a matrix member")
