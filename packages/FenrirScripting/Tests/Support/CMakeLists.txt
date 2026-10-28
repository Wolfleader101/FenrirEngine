# The shared test support: the four headers every scripting test executable uses (the headless app, the probe, the
# extension test app, the native call harness) and the harness's implementation. The harness is the one test seam over
# FenrirScripting's private modules (the change buffer, the registry), so this library alone is given the package's
# `src`; a test executable links it and sees only the public include directory. No header here needs Catch2
# (ExtensionTestApp.hpp includes Catch2's macros, which the executable including it supplies), so the library links
# FenrirScripting alone.
add_library(FenrirScriptingTestSupport STATIC
    NativeCallHarness.cpp
    ExtensionTestApp.hpp
    HeadlessApp.hpp
    NativeCallHarness.hpp
    ScriptingProbe.hpp
)
target_include_directories(FenrirScriptingTestSupport
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../src"
)
target_link_libraries(FenrirScriptingTestSupport PUBLIC FenrirScripting)
# The headless app and the extension test app read the environment with std::getenv, which MSVC's runtime calls unsafe;
# the headers are compiled by whoever includes them, so the definition is public.
if(MSVC)
    target_compile_definitions(FenrirScriptingTestSupport PUBLIC _CRT_SECURE_NO_WARNINGS)
endif()
