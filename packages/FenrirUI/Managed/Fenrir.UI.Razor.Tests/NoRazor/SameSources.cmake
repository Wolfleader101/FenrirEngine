# Builds the NoRazor project twice, without and with Fenrir.UI.Razor.props, writing the generators' sources out, and
# fails unless both builds generated the same files with the same text: a script project with no .razor file is left
# as it was. Run by CTest in script mode:
#   cmake -DDOTNET=... -DPROJECT=<NoRazor.csproj> -DSDK=<scripting SDK dir>/ -DRAZOR=<Razor folder>/ -DWORK=<dir>
#       -P SameSources.cmake
foreach(required DOTNET PROJECT SDK RAZOR WORK)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "SameSources: ${required} is not set")
    endif()
endforeach()

foreach(variant plain razor)
    set(import false)
    if(variant STREQUAL "razor")
        set(import true)
    endif()
    file(REMOVE_RECURSE "${WORK}/${variant}")
    execute_process(
        COMMAND "${DOTNET}" build "${PROJECT}" --configuration Release -nologo -v:q --no-incremental
            "-p:FenrirScriptingSdk=${SDK}" "-p:FenrirUIRazorContractDir=${RAZOR}" "-p:ImportRazor=${import}"
            "-p:EmitCompilerGeneratedFiles=true" "-p:CompilerGeneratedFilesOutputPath=${WORK}/${variant}/generated"
            "-p:BaseIntermediateOutputPath=${WORK}/${variant}/obj/" "-p:OutputPath=${WORK}/${variant}/bin/"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error_output
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "The ${variant} build of NoRazor failed:\n${output}${error_output}")
    endif()
    file(GLOB_RECURSE ${variant}_files RELATIVE "${WORK}/${variant}/generated" "${WORK}/${variant}/generated/*")
    list(SORT ${variant}_files)
endforeach()

if(NOT plain_files)
    message(FATAL_ERROR "The plain build generated nothing, so the comparison proves nothing.")
endif()
if(NOT plain_files STREQUAL razor_files)
    message(FATAL_ERROR "With Fenrir.UI.Razor.props the generated files changed.\nWithout: ${plain_files}\nWith: ${razor_files}")
endif()
foreach(file IN LISTS plain_files)
    file(READ "${WORK}/plain/generated/${file}" plain_text)
    file(READ "${WORK}/razor/generated/${file}" razor_text)
    if(NOT plain_text STREQUAL razor_text)
        message(FATAL_ERROR "With Fenrir.UI.Razor.props ${file} changed.")
    endif()
endforeach()
list(LENGTH plain_files count)
message(STATUS "Both builds generated the same ${count} file(s).")
