# Builds one case of the CompileFail project and checks the result. Run by CTest in script mode:
#   cmake -DDOTNET=... -DPROJECT=... -DSDK=<sdk dir>/ -DCASE=<name> -DWORK=<dir> -DEXPECT=<OK|error code> [-DALSO=<error code>]
#       [-DPROPERTY=<Name>=<value>] -P ExpectCompile.cmake
# PROPERTY, when set, is one more MSBuild property the project needs (an extension's contract folder, say).
# EXPECT=OK means the case must compile. An error code (CS8345) means the build must fail, and every error it reports
# must be that one (or ALSO, one error the compiler adds on its own beside it), so a case cannot pass because something
# unrelated broke.
foreach(required DOTNET PROJECT SDK CASE WORK EXPECT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "ExpectCompile: ${required} is not set")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK}/${CASE}")
set(extra_property "")
if(DEFINED PROPERTY AND NOT PROPERTY STREQUAL "")
    set(extra_property "-p:${PROPERTY}")
endif()
execute_process(
    COMMAND "${DOTNET}" build "${PROJECT}" --configuration Release -nologo -v:q --no-incremental
        "-p:FenrirScriptingSdk=${SDK}" "-p:Case=${CASE}" ${extra_property}
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
    # ALSO, when set, is one more error the case may report beside EXPECT (one the compiler adds on its own).
    if(DEFINED ALSO AND NOT ALSO STREQUAL "")
        list(REMOVE_ITEM errors "error ${ALSO}")
    endif()
    if(NOT errors STREQUAL "error ${EXPECT}")
        message(FATAL_ERROR "The case '${CASE}' should fail with only ${EXPECT} (and ALSO, if given), but reported: ${errors}\n${everything}")
    endif()
endif()
message(STATUS "${CASE}: ${EXPECT} as expected")
