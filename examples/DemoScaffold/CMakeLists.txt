# The demo scaffold (examples/DemoScaffold/README.md): the FenrirDemoScaffold static library every demo app links, and
# fenrir_add_demo_app, which makes a demo app. Built with the examples when the renderer is on.
add_library(FenrirDemoScaffold STATIC
    include/FenrirDemoScaffold/DemoScaffold.hpp
    src/DemoScaffold.cpp
)
target_include_directories(FenrirDemoScaffold PUBLIC include)
target_link_libraries(FenrirDemoScaffold
    PUBLIC FenrirApp FenrirRenderer FenrirInput FenrirMath FenrirECS
    PRIVATE FenrirWindow FenrirAssets FenrirDebugDraw FenrirLogger
)

include(cmake/FenrirDemoApp.cmake)

# The scaffold's own tests, on the null backend: the scene list, the free-fly camera and the smoke runs.
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
