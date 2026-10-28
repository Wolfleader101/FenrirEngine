# FenrirPhysics's scripting extension (ADR-0009, ADR-0046): the Fenrir.Physics C# contract and the FenrirPhysicsScripting
# library holding the native functions behind it, built by FenrirScripting's extension module
# (packages/FenrirScripting/cmake/FenrirScriptingExtension.cmake). It uses FenrirPhysics's public headers only: its
# natives reach the active scene's PhysicsWorld, and keep their own state as the extension's state. The root build adds
# this directory only when FENRIR_ENABLE_PHYSICS and FENRIR_ENABLE_SCRIPTING are both on. The binding
# (api/FenrirPhysics.binding.toml) binds FenrirPhysics's components, events and shared structs, the query types among
# them (../api/FenrirPhysics.api.json); this library describes nothing of its own.
fenrir_scripting_contract(Physics)
fenrir_scripting_extension(Physics
    SOURCES
        include/FenrirPhysicsScripting/PhysicsScripting.hpp
        src/PhysicsScripting.cpp
    LINK FenrirPhysics
    NO_SELECTION
)
