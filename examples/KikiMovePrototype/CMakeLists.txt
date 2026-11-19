# PROTOTYPE, throwaway: Flightless Frenzy's "How does Kiki move?" grey box (README.md). Physics only, no scripting.
fenrir_add_demo_app(KikiMovePrototype SOURCES src/main.cpp)
target_link_libraries(KikiMovePrototype PRIVATE FenrirPhysics FenrirAssets FenrirDebugDraw FenrirWindow)
