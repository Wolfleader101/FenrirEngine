add_subdirectory(entt INTERFACE)

target_include_directories(FenrirECS PUBLIC
    entt/src/
)