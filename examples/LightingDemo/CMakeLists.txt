# LightingDemo (examples/LightingDemo/README.md): the lighting spec's demo app, one scene per lighting showcase. Its
# committed assets are in assets/; the large ones are fetched (fetched-assets.json, ADR-0052).
fenrir_add_demo_app(LightingDemo
    SOURCES
        src/main.cpp
        src/LightingScenes.hpp
        src/LightingPanel.hpp
        src/LightingPanel.cpp
        src/FetchedAssets.hpp
        src/FetchedAssets.cpp
        src/MaterialGrid.cpp
        src/ShowcaseModels.cpp
    ASSETS assets
)
# The grid's labels and the missing-assets message are drawn through the scene's DebugDraw.
target_link_libraries(LightingDemo PRIVATE FenrirDebugDraw FenrirAssets)

# With FENRIR_FETCH_DEMO_ASSETS, configure downloads every file of the manifest into the cache, and the app loads them
# from there: they are never copied into the build.
if(FENRIR_FETCH_DEMO_ASSETS)
    fenrir_fetch_demo_assets(MANIFEST fetched-assets.json CACHE_DIR "${FENRIR_DEMO_ASSET_CACHE}")
    target_compile_definitions(LightingDemo PRIVATE LIGHTING_DEMO_FETCHED_ASSETS="${FENRIR_DEMO_ASSET_CACHE}")
endif()
