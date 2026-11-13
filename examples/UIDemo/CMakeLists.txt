# UIDemo (examples/UIDemo/README.md): what a game gets from FenrirUI, over a lit room. Its code is UIDemoCore, which the
# app and its tests both link; its HUD's document and stylesheet are in assets/ui, read from this source tree while it
# is there (UIDEMO_SOURCE_ASSETS), so an edit to them hot-reloads and a rebuild keeps it.
add_library(UIDemoCore STATIC
    src/UIDemo.hpp
    src/DemoConfig.cpp
    src/Hud.cpp
    src/Compass.hpp
    src/Compass.cpp
    src/Room.cpp
)
target_include_directories(UIDemoCore PUBLIC src)
target_link_libraries(UIDemoCore PUBLIC FenrirDemoScaffold FenrirUI FenrirAssets PRIVATE FenrirPainter)
target_compile_definitions(UIDemoCore PRIVATE UIDEMO_SOURCE_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets")

fenrir_add_demo_app(UIDemo SOURCES src/main.cpp ASSETS assets)
target_link_libraries(UIDemo PRIVATE UIDemoCore FenrirLogger)

# The HUD's tests: the app's own setup, on the null backend.
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
