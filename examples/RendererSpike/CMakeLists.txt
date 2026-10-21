# SPIKE (prototype/fenrir-renderer-spike, throwaway): the renderer spike app.
add_executable(RendererSpike src/main.cpp)
target_link_libraries(RendererSpike PRIVATE FenrirApp FenrirAssets FenrirDebugDraw FenrirInput FenrirRenderer
                                            FenrirWindow FenrirLogger)
