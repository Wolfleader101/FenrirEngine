# FenrirPhysics's scripting extension (ADR-0009): the Fenrir.Physics C# contract and the FenrirPhysicsScripting library
# holding the native functions behind it. A library of its own so FenrirPhysics never links FenrirScripting. It uses
# FenrirPhysics's public headers only: its natives reach the active scene's PhysicsWorld, and keep their own state as
# the extension's state. The root build adds this directory only when FENRIR_ENABLE_PHYSICS and FENRIR_ENABLE_SCRIPTING
# are both on.

# The contract, built against the scripting SDK with the .NET SDK Iris found. fenrir_deploy_physics_scripting copies it
# next to an executable, beside the scripting contract, where Scripting::Install finds the extension's relative path.
set(FENRIR_PHYSICS_CONTRACT_DIR "${CMAKE_BINARY_DIR}/fenrir-physics/contract" CACHE INTERNAL "Where the Fenrir.Physics contract is built")
set(fenrir_physics_managed_dir "${CMAKE_CURRENT_SOURCE_DIR}/../Managed/Fenrir.Physics")
file(GLOB fenrir_physics_contract_sources CONFIGURE_DEPENDS "${fenrir_physics_managed_dir}/*.cs")
add_custom_command(
    OUTPUT "${FENRIR_PHYSICS_CONTRACT_DIR}/Fenrir.Physics.dll"
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${fenrir_physics_managed_dir}/Fenrir.Physics.csproj"
        --configuration Release --output "${FENRIR_PHYSICS_CONTRACT_DIR}" -nologo -v:q
        "-p:FenrirScriptingSdk=${FENRIR_SCRIPTING_CONTRACT_DIR}/"
        "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/contract-obj/"
    DEPENDS ${fenrir_physics_contract_sources} "${fenrir_physics_managed_dir}/Fenrir.Physics.csproj"
        FenrirScriptingContract
    COMMENT "Building the Fenrir.Physics contract"
    VERBATIM
)
add_custom_target(FenrirPhysicsContract ALL DEPENDS "${FENRIR_PHYSICS_CONTRACT_DIR}/Fenrir.Physics.dll")
add_dependencies(FenrirPhysicsContract FenrirScriptingContract)

# fenrir_deploy_physics_scripting(<target>): after <target> builds, copy the Fenrir.Physics contract next to it. Call it
# with fenrir_deploy_scripting(<target>) on an executable that adds PhysicsScriptingExtension() to its scripting config.
function(fenrir_deploy_physics_scripting target)
    add_custom_target(${target}DeployPhysicsContract
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${FENRIR_PHYSICS_CONTRACT_DIR}/Fenrir.Physics.dll"
            "$<TARGET_FILE_DIR:${target}>"
        VERBATIM
    )
    add_dependencies(${target}DeployPhysicsContract FenrirPhysicsContract)
    add_dependencies(${target} ${target}DeployPhysicsContract)
endfunction()

add_library(FenrirPhysicsScripting STATIC
    include/FenrirPhysicsScripting/PhysicsScripting.hpp
    src/PhysicsScripting.cpp
)
target_include_directories(FenrirPhysicsScripting PUBLIC include)
target_link_libraries(FenrirPhysicsScripting PUBLIC FenrirPhysics FenrirScripting)

if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
