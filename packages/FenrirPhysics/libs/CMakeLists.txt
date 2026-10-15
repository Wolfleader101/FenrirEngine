# JoltPhysics, FenrirPhysics's physics backend (ADR-0010), with the settings decided in "How does Jolt join the build?"
# (.scratch/fenrir-physics/issues/13-how-jolt-joins-the-build.md). Everything set here stays in this directory, so none
# of it reaches the rest of the engine.
#
# Debug and Release match Jolt's configurations. RelWithDebInfo and MinSizeRel build Jolt without its debug-only checks.

# The engine's directory-wide warning flags (-Weverything, /W4) stop here: Jolt's warnings are not ours to fix.
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

# FenrirPhysics links Jolt PRIVATE, so no other target sees it; to FenrirPhysics's own sources its headers are system
# headers.
set_target_properties(Jolt PROPERTIES SYSTEM TRUE)
