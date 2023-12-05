set(BUILD_SHARED_LIBS OFF)
set(SPDLOG_BUILD_SHARED OFF)

add_subdirectory(entt INTERFACE)

target_include_directories(FenrirLogger PUBLIC
    entt/src
)