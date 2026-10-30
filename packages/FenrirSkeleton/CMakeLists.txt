# FenrirSkeleton: skeleton instances, their reads, the pose pipeline and its stages, bone attachments, the skinning
# source and spawning (ADR-0028, ADR-0031, ADR-0033, ADR-0037, ADR-0041). It links no library and has no GPU code. Built
# with FENRIR_ENABLE_ASSETS, with no option of its own; FenrirRenderer reaches it only through FenrirAssets' SkinningSource, and links nothing
# from it.
add_library(FenrirSkeleton STATIC
    include/FenrirSkeleton/SkeletonPose.hpp
    include/FenrirSkeleton/PoseStages.hpp
    include/FenrirSkeleton/Skeletons.hpp
    include/FenrirSkeleton/BoneAttachment.hpp

    src/SkeletonPoses.hpp
    src/SkeletonPoses.cpp
    src/PoseEdit.cpp
    src/SkeletonsState.hpp
    src/Skeletons.cpp
    src/Attachments.hpp
    src/Attachments.cpp
    src/BoneName.cpp
)

target_link_libraries(FenrirSkeleton PUBLIC FenrirAssets FenrirECS FenrirScene FenrirApp FenrirMath FenrirTime)

target_include_directories(FenrirSkeleton PUBLIC include PRIVATE src)

# FenrirRenderer and FenrirPhysics must link nothing from this package (ADR-0041): checked once every target exists.
function(fenrir_skeleton_check_unlinked)
    foreach(user FenrirRenderer FenrirPhysics)
        if(TARGET ${user})
            get_target_property(links ${user} LINK_LIBRARIES)
            if(links AND "FenrirSkeleton" IN_LIST links)
                message(FATAL_ERROR "${user} links FenrirSkeleton: it may reach skeletons only through SkinningSource.")
            endif()
        endif()
    endforeach()
endfunction()
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL fenrir_skeleton_check_unlinked)

# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON (see the root CMakeLists.txt).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

# Its shared types (ADR-0042): `cmake --build <dir> --target fenrir-describe-api` regenerates api/FenrirSkeleton.api.json,
# which FenrirSkeletonScripting's binding binds.
fenrir_describe_api(FenrirSkeleton api/FenrirSkeleton.api.toml)
