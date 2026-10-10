add_executable(IrisHostingExample main.cpp)
target_link_libraries(IrisHostingExample PRIVATE Iris)
set_target_properties(IrisHostingExample PROPERTIES CXX_EXTENSIONS OFF)

# Deploy the native dependency beside this example on every supported platform.
add_custom_command(TARGET IrisHostingExample POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:IrisNethost>" "$<TARGET_FILE_DIR:IrisHostingExample>"
    VERBATIM
)

if(APPLE)
    set_target_properties(IrisHostingExample PROPERTIES BUILD_RPATH "@loader_path")
elseif(UNIX)
    set_target_properties(IrisHostingExample PROPERTIES BUILD_RPATH "$ORIGIN")
endif()
