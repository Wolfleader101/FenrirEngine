cmake_minimum_required(VERSION 3.20)
project(FenrirExamples)

# The demo apps (GLOSSARY.md), on the demo scaffold. They draw, so they exist only with the renderer.
if(FENRIR_BUILD_EXAMPLES AND FENRIR_ENABLE_RENDERER)
    add_subdirectory(DemoScaffold)
    add_subdirectory(RendererDemo)
endif()

# The legacy DemoApp, until the demo apps replace it.
if(FENRIR_BUILD_LEGACY_DEMOAPP)
    add_subdirectory(DemoApp)
endif()
