# Builds one case of the CompileFail project and checks the result. Run by CTest in script mode:
#   cmake -DDOTNET=... -DPROJECT=... -DSDK=<sdk dir>/ -DCASE=<name> -DWORK=<dir> -DEXPECT=<OK|error code> -P ExpectCompile.cmake
# EXPECT=OK means the case must compile. An error code (CS8345) means the build must fail, and every error it reports
# must be that one, so a case cannot pass because something unrelated broke.
foreach(required DOTNET PROJECT SDK CASE WORK EXPECT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "ExpectCompile: ${required} is not set")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK}/${CASE}")
execute_process(
    COMMAND "${DOTNET}" build "${PROJECT}" --configuration Release -nologo -v:q --no-incremental
        "-p:FenrirScriptingSdk=${SDK}" "-p:Case=${CASE}"
        "-p:BaseIntermediateOutputPath=${WORK}/${CASE}/obj/" "-p:OutputPath=${WORK}/${CASE}/bin/"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error_output
)
set(everything "${output}${error_output}")

if(EXPECT STREQUAL "OK")
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "The control case '${CASE}' should compile but did not:\n${everything}")
    endif()
else()
    if(result EQUAL 0)
        message(FATAL_ERROR "The case '${CASE}' compiled, but ${EXPECT} was expected.")
    endif()
    string(REGEX MATCHALL "error [A-Z]+[0-9]+" errors "${everything}")
    list(REMOVE_DUPLICATES errors)
    if(NOT errors STREQUAL "error ${EXPECT}")
        message(FATAL_ERROR "The case '${CASE}' should fail with only ${EXPECT}, but reported: ${errors}\n${everything}")
    endif()
endif()
message(STATUS "${CASE}: ${EXPECT} as expected")
