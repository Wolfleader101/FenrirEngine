# Runs FenrirShaderCompiler on one failing fixture and checks it fails as expected. Run by CTest in script mode:
#   cmake -DTOOL=<FenrirShaderCompiler> -DVERTEX=<file> -DFRAGMENT=<file> -DINCLUDE=<dir> -DWORK=<dir> -DEXPECT=<regex>
#       -P ExpectBuildFailure.cmake
# The build must exit non-zero, print a message matching EXPECT, and leave no container behind.
foreach(required TOOL VERTEX FRAGMENT INCLUDE WORK EXPECT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "ExpectBuildFailure: ${required} is not set")
    endif()
endforeach()

set(container "${WORK}/Case.fshader")
file(REMOVE_RECURSE "${WORK}")
execute_process(
    COMMAND "${TOOL}" --name Case --vertex "${VERTEX}" --fragment "${FRAGMENT}" --include "${INCLUDE}"
            --output "${container}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error_output
)
set(everything "${output}${error_output}")

if(result EQUAL 0)
    message(FATAL_ERROR "The build should have failed with '${EXPECT}', but succeeded:\n${everything}")
endif()
if(NOT everything MATCHES "${EXPECT}")
    message(FATAL_ERROR "The build failed, but without '${EXPECT}':\n${everything}")
endif()
if(EXISTS "${container}")
    message(FATAL_ERROR "The build failed but still wrote a container: ${container}")
endif()
message(STATUS "Failed as expected:\n${everything}")
