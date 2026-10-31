# FenrirAnimation: clip playback through the Animator, sampled into FenrirSkeleton's base pose in its Animate stage,
# with clip events as ECS events (ADR-0028, ADR-0034). Its runtime is its own, on glm; it links no library and has no
# GPU code. Built with FENRIR_ENABLE_ASSETS, with no option of its own; FenrirRenderer and FenrirPhysics link nothing
# from it.
add_library(FenrirAnimation STATIC
    include/FenrirAnimation/Animator.hpp
    include/FenrirAnimation/AnimationLayer.hpp
    include/FenrirAnimation/AnimationEvents.hpp
    include/FenrirAnimation/Animation.hpp

    src/AnimationState.hpp
    src/Animators.hpp
    src/Animators.cpp
    src/Bindings.hpp
    src/Bindings.cpp
    src/PlaybackCursor.hpp
    src/PlaybackCursor.cpp
    src/Blending.hpp
    src/Blending.cpp
    src/Sampling.hpp
    src/Sampling.cpp
    src/Animation.cpp
)

target_link_libraries(FenrirAnimation PUBLIC FenrirSkeleton FenrirAssets)

target_include_directories(FenrirAnimation PUBLIC include PRIVATE src)

# FenrirRenderer and FenrirPhysics must link nothing from this package (ADR-0028): checked once every target exists.
function(fenrir_animation_check_unlinked)
    foreach(user FenrirRenderer FenrirPhysics)
        if(TARGET ${user})
            get_target_property(links ${user} LINK_LIBRARIES)
            if(links AND "FenrirAnimation" IN_LIST links)
                message(FATAL_ERROR "${user} links FenrirAnimation, which nothing outside the animation stack may.")
            endif()
        endif()
    endforeach()
endfunction()
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL fenrir_animation_check_unlinked)

# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON (see the root CMakeLists.txt).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

# Its shared types (ADR-0042): `cmake --build <dir> --target fenrir-describe-api` regenerates api/FenrirAnimation.api.json,
# which FenrirAnimationScripting's binding binds.
fenrir_describe_api(FenrirAnimation api/FenrirAnimation.api.toml)
