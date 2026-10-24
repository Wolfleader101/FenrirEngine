# Installs this Iris build, then configures, builds and runs the standalone consumer project against the
# installed package. Run with cmake -P and these variables:
#   IRIS_BUILD_DIR, IRIS_CONFIG, CONSUMER_SOURCE_DIR, WORK_DIR, GENERATOR, PLATFORM, COMPONENT
function(run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    message("${output}")
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Command failed (${result}): ${ARGN}")
    endif()
endfunction()

set(prefix "${WORK_DIR}/install")
set(build "${WORK_DIR}/build")
file(REMOVE_RECURSE "${WORK_DIR}")

run("${CMAKE_COMMAND}" --install "${IRIS_BUILD_DIR}" --config "${IRIS_CONFIG}" --prefix "${prefix}")

set(generator_arguments -G "${GENERATOR}")
if(PLATFORM)
    list(APPEND generator_arguments -A "${PLATFORM}")
endif()
run("${CMAKE_COMMAND}" -S "${CONSUMER_SOURCE_DIR}" -B "${build}" ${generator_arguments}
    "-DCMAKE_PREFIX_PATH=${prefix}" "-DCMAKE_BUILD_TYPE=${IRIS_CONFIG}")
run("${CMAKE_COMMAND}" --build "${build}" --config "${IRIS_CONFIG}")

file(GLOB_RECURSE consumer "${build}/IrisConsumer" "${build}/IrisConsumer.exe")
list(LENGTH consumer count)
if(NOT count EQUAL 1)
    message(FATAL_ERROR "Expected one built IrisConsumer, found: ${consumer}")
endif()
run("${consumer}" "${COMPONENT}")
