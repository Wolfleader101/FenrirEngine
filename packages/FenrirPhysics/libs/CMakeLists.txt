# PROTOTYPE (physics spike): Jolt with the settings from "How does Jolt join the build?".

# The engine's directory-wide warning flags (-Weverything, /W4) stop here: we do not own Jolt's warnings.
set_directory_properties(PROPERTIES COMPILE_OPTIONS "")

set(JPH_BUILD_SHARED_LIBS OFF)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF)
set(INTERPROCEDURAL_OPTIMIZATION OFF)
set(CPP_RTTI_ENABLED ON)
set(CPP_EXCEPTIONS_ENABLED OFF)
set(JPH_USE_DX12 OFF)
set(JPH_USE_VK OFF)
set(JPH_USE_MTL OFF)
set(JPH_USE_CPU_COMPUTE OFF)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF)
set(ENABLE_OBJECT_STREAM OFF)
set(CROSS_PLATFORM_DETERMINISTIC OFF)
set(ENABLE_ALL_WARNINGS OFF)
set(ENABLE_INSTALL OFF)

add_subdirectory(JoltPhysics/Build)

# Users of FenrirPhysics never see Jolt (linked PRIVATE); its headers are system headers to FenrirPhysics's own sources.
set_target_properties(Jolt PROPERTIES SYSTEM TRUE)
