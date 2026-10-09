add_executable(FenrirHostingExample main.cpp)
target_link_libraries(FenrirHostingExample PRIVATE FenrirDotNetHost)
set_target_properties(FenrirHostingExample PROPERTIES CXX_EXTENSIONS OFF)

# Deploy the native dependency beside this example on every supported platform.
add_custom_command(TARGET FenrirHostingExample POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:FenrirScriptingNethost>" "$<TARGET_FILE_DIR:FenrirHostingExample>"
    VERBATIM
)

if(APPLE)
    set_target_properties(FenrirHostingExample PROPERTIES BUILD_RPATH "@loader_path")
elseif(UNIX)
    set_target_properties(FenrirHostingExample PROPERTIES BUILD_RPATH "$ORIGIN")
endif()
