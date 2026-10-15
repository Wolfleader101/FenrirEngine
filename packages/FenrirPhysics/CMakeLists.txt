# PROTOTYPE (physics spike)
add_library(FenrirPhysics STATIC
    include/FenrirPhysics/Components.hpp
    include/FenrirPhysics/Physics.hpp

    src/Physics.cpp
    src/PhysicsWorld.cpp

    src/Backend/Backend.hpp
    src/Backend/Jolt/JoltBackend.cpp
)

add_subdirectory(libs)

target_link_libraries(FenrirPhysics PUBLIC FenrirApp FenrirScene FenrirECS FenrirMath PRIVATE Jolt)

target_include_directories(FenrirPhysics PUBLIC include PRIVATE src)

# PROTOTYPE: headless check of the spike (uses only the public interface).
add_executable(FenrirPhysicsSpikeCheck spike/SpikeCheck.cpp)
target_link_libraries(FenrirPhysicsSpikeCheck PRIVATE FenrirPhysics FenrirLogger)
